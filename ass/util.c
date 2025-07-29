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


char* proxy_header_factory(struct linkedlist* header_fields);
int process_body(struct linkedlist* header_fields, int sock, char** body_buffer, char* buffer, int* inbuf, int* req_body_length);
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
        printf("-- trying sockets\n");
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

int proxyMessageSend(char** header, char** body, 
    struct linkedlist* header_fields, int sfd, char* buf, int* inbuf_used, int* req_body_length) {
    *header = proxy_header_factory(header_fields);

    if(process_body(header_fields, sfd, body, buf, inbuf_used, req_body_length) == -1) {
        return -1;
    }
    return 1;
}

/* - private - */

/* To make the new proxy request header */
char* proxy_header_factory(struct linkedlist* header_fields) {
    header_fields->insert(header_fields, "Connection", "close");
    // header_fields->insert(header_fields, "Via", "1.1 z5489321");
    header_fields->delete(header_fields, "proxy-connection");

    /* get header size */
    size_t header_len = 5; // an extra 5 bytes to be safe
    for(node* it = header_fields->head; it != NULL; it = it->next) {
        header_len += strlen(it->key) + 2 + strlen(it->value) + 2 + 1;
    }

    /* allocate memory for header block */
    char* proxy_header = malloc(header_len + 1);
    if(!proxy_header) {
        printf("header allocation failed!\n");
    }

    /* put header in header block */
    size_t offset;

    char* top = header_fields->search(header_fields, "top");
    offset = sprintf(proxy_header, "%s", top);  // the "top line" already has "\r\n", look at proxy.c
    header_fields->delete(header_fields, "top");

    for(node* it = header_fields->head; it != NULL; it = it->next) {
        int written = sprintf(proxy_header + offset, "%s: %s\r\n", it->key, it->value);
        offset += written;
    }
    sprintf(proxy_header + offset, "\r\n");

    return proxy_header;
}

// to process the request body returns buffer containing body
int process_body(struct linkedlist* header_fields, int sock, char** body_buffer, char* buffer, int* inbuf, int* req_body_length) {
    *body_buffer = NULL;
    char* str_content_length = header_fields->search(header_fields, "Content-Length");
    if(str_content_length == NULL) {
        return 1;
    }

    int content_length = atoi(str_content_length);
    *req_body_length = content_length;

    if(content_length == 0) {
        return 1;
    }

    int buffer_len = content_length + 10;
    *body_buffer = malloc(buffer_len);
    if(inbuf > 0) memmove(*body_buffer, buffer, *inbuf);

    if(recv_message(sock, *body_buffer + *inbuf, content_length - *inbuf) == -1) {
        printf("ERROR: recv request body failed\n");
        return -1;
    }

    return 1;
}

char* skipLeadingWhitespace(char* str) {
    while(isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}
