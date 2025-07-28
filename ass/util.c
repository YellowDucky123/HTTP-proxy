#include <netdb.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>

#include "linkedlist.h"

// 
#define HEAD 1
#define GET 2
#define POST 3
#define CONNECT 4

char* skipLeadingWhitespace(char* str);

int getSocketFD(char* host) {
    printf("Connecting to - %s\n", host);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    struct addrinfo *result, *rp;
    int sfd;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    int s = getaddrinfo(host, "http", &hints, &result);
    if (s != 0) {
        printf("ERROR - getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    /* getaddrinfo() returns a list of address structures.
        Try each address until we successfully connect(2).
        If socket(2) (or connect(2)) fails, we (close the socket
        and) try the next address. 
    */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        printf("-- trying socket\n");
        sfd = socket(rp->ai_family, rp->ai_socktype,
                    rp->ai_protocol);
        if (sfd == -1)
            continue;
        
        // Set the TCP socket to non-blocking mode
        // fcntl(sfd, F_SETFL, O_NONBLOCK);

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */

        close(sfd);
    }

    freeaddrinfo(result);           /* No longer needed */

    if (rp == NULL) {               /* If no address succeeded */
        fprintf(stderr, "ERROR - Could not connect\n");
        return -1;
    }
    return sfd;
}


// Returns 1 if end of header is reached
int process_header_data(
    int sock,
    struct linkedlist* header_fields, 
    char* buffer, 
    int buffer_len,
    int* inbuf_used
) {
    /* Scan for newlines in the line buffer; we're careful here to deal with embedded \0s
	* an evil server may send, as well as only processing lines that are complete.
	*/

    int keep_going = 1;
    while(keep_going) {
        int overBuffer = 0;

        char* line_start = buffer;
        char* line_end;
        while ((line_end = (char*)memchr((void*)line_start, '\n', *inbuf_used - (line_start - buffer)))) {
            *line_end = 0; // Set null to divide line
            if(strlen(line_start) == 1 && strcmp(line_start, "\r") == 0) {
                keep_going = 0;
                break;
            }
            *(line_end - 1) = 0;
            printf("> Parsing header line: %s\n", line_start);
            // printf("line length: %x, %d\n", line_start[0], keep_going);
            // printf("r is %x\n", '\r');
            char* field, *data;
            char* divider = strchr(line_start, ':'); // tokenise with ':'
            if(divider == NULL) {
                printf("ERROR - Malformed header line (missing colon): %s\n", line_start);
                return -1;
            }

            *divider = '\0';
            field = line_start; /* Start to ':'  (pointer) */
            data = skipLeadingWhitespace(divider + 1); /* ':' to end without the leading space (pointer) */

            if(strlen(data) == 0) {
                printf("ERROR - Malformed header line (no data): %s\n", field);
                return -1;
            }
            
            if(strcasecmp(field, "host") != 0) {
                header_fields->insert(header_fields, field, data);
            } 

            line_start = line_end + 1;
        }

        if(keep_going == 0) {
            int byte_left = *inbuf_used - (line_start - buffer);
            if(byte_left > 2) { // if inbuf <= 2 then there's nothing left
                line_start = line_end + 1;
                *inbuf_used -= (line_start - buffer);
                if(*inbuf_used > 0) memmove(buffer, line_start, *inbuf_used);
                byte_left -= 2;
            } else {
                byte_left = 0;
            }
            *inbuf_used = byte_left;    // updates the amount of bytes used in the buffer after header finish
            break;
        }
        
        if(*inbuf_used - (line_start - buffer) > 0) {
            /* Shift buffer down so the unprocessed data is at the start */
            *inbuf_used -= (line_start - buffer);
            if(*inbuf_used > 0) memmove(buffer, line_start, *inbuf_used);
        }

        int rv;
        if((rv = recv(sock, buffer + (*inbuf_used), buffer_len - (*inbuf_used), 0)) <= 0) {
            printf("ERROR - recv error\n");
            return -1;
        }
        (*inbuf_used) += rv;
    }

    return 1;
}

int send_message(int sfd, char* body, int body_length) {
    int offset = 0;
    while(body_length > 0) {
        int bytes = send(sfd, body + offset, body_length, 0);

        if (bytes < 0) {
            perror("ERROR: send failed");
            return -1;
        }

        body_length -= bytes;
        offset += bytes;
    }
    if(body_length < 0) return -1;
    return 1;
}

int recv_message(int sock, char* body, int content_length) {
    int acumulate_byte = 0;
    while(acumulate_byte < content_length) {
        int rv;
        if((rv = recv(sock, body + acumulate_byte, content_length - acumulate_byte, 0)) < 0) {
            printf("read response body fail\n");
            return -1;
        }
        if(rv == 0) break;
        acumulate_byte += rv;
    }

    return 1;
}

int isChunkedTransferEncoding(struct linkedlist* response_fields) {
    char* val = response_fields->search(response_fields, "transfer-encoding");
    if(val == NULL) return 0;

    printf(">Transfer-Encoding: %s\n", val);

    char* encoding = strtok(val, ",");  
    while(encoding != NULL) {
        if(strcasecmp(encoding, "chunked") == 0) {
            free(val);
            return 1;
        }
        encoding = strtok(NULL, ",");
    }

    free(val);
    return 0;
}

// int receive_response(int sock, int* status_code, 
//     struct linkedlist* response_fields, int* request_method, char** header, int* body_length, char** body) {
    
//     char* respond_buffer = NULL;
//     int inbuf;
//     *header = responseHeader(sock, &respond_buffer, &inbuf, status_code, response_fields);
//     if(*header == NULL) {
//         return -1;
//     }

//     if(inbuf > 0) {
//         *body = respond_buffer;
//     }

//     int b = responseBody(sock, response_fields, request_method, status_code, body_length, body);
//     if(b == -1) {
//         return -1;
//     }
// }

// /* Receive response from server and forward to client */
// char* responseHeader(int sock, char** buf, int *inbuf, int* status_code, struct linkedlist* response_fields) {
//     int respond_buffer_len = 1024;
//     *buf = malloc(respond_buffer_len);
//     char* respond_buffer = *buf;

//     int inbuf_used = 0;
//     int rv;
//     if((rv = recv(sock, respond_buffer, respond_buffer_len, 0)) < 0) {
//         printf("ERROR: recv response failed!\n");
//         return NULL;
//     }

//     inbuf_used = rv;

//     char* line_start = respond_buffer;
//     char* line_end;

//     /* Parse the response line */
//     line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - respond_buffer));
//     *line_end = 0;  // NULL

//     char* space1 = strchr(line_start, ' ');
//     char* space2 = strchr(space1 + 1, ' ');
//     *space2 = 0;
//     char* status_code_str = strdup(space1 + 1);
//     char* status_message = strdup(space2 + 1);

//     *status_code = atoi(status_code_str);

//     line_start = line_end + 1;
//     inbuf_used -= (line_start - respond_buffer);
//     memmove(respond_buffer, line_start, inbuf_used);
//     respond_buffer[inbuf_used] = 0;

//     /* Parse the response headers */
//     process_header_data(sock, response_fields, respond_buffer, respond_buffer_len, &inbuf_used);
//     *inbuf = inbuf_used;
//     response_fields->insert(response_fields, "Via", "1.1 z5489321");
 
//     /* Make the header */
//     size_t bytes = strlen(status_code_str) + strlen(status_message) + 10 + 5;   // padding bytes
//     for(node* it = response_fields->head; it != NULL; it = it->next) {
//         bytes += strlen(it->key) + 2 + strlen(it->value) + 2 + 1;
//     }

//     char* return_buffer = malloc(bytes + 1);

//     printf("> status code: %s | status message: %s\n", status_code_str, status_message);

//     int offset = sprintf(return_buffer, "HTTP/1.1 %s %s\r\n", status_code_str, status_message);
//     for(node* it = response_fields->head; it != NULL; it = it->next) {
//         int written = sprintf(return_buffer + offset, "%s: %s\r\n", it->key, it->value);
//         offset += written;
//     }
//     sprintf(return_buffer + offset, "\r\n");

//     free(status_code_str);
//     free(status_message);
//     return return_buffer;
// }


// int responseBody(int sock, 
//     struct linkedlist* response_fields, int* request_method, int* status_code, int* body_length, char** body) {
//     if((*request_method) == HEAD || (*status_code) == 204 || (*status_code) == 304) {
//         return 1;
//     }

//     /* Get content length and check if there is a body */
//     int content_length = atoi(response_fields->search(response_fields, "content-length"));
//     *body_length = content_length;
//     if(content_length == 0) {
//         return 1;
//     }
    
//     // if a body buffer does not exist yet, make one
//     *body = (*body == NULL) ? malloc(content_length) : *body;

//     if(recv_message(sock, *body, content_length) == -1) {
//         return -1;
//     }

//     return 1;
// }

/* - private - */

char* skipLeadingWhitespace(char* str) {
    while(isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}
