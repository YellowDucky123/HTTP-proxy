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

#include "linkedlist.h"

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
        if(keep_going == 0) break;

        /* Shift buffer down so the unprocessed data is at the start */
        *inbuf_used -= (line_start - buffer);
        memmove(buffer, line_start, *inbuf_used);

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
    while(body_length > 0) {
        int bytes = send(sfd, body, body_length, 0);
        body_length -= bytes;
    }
    if(body_length < 0) return -1;
    return 1;
}

/* - private - */

char* skipLeadingWhitespace(char* str) {
    while(isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}