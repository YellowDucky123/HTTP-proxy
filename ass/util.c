#include <netdb.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>

#include "linkedlist.h"

int getSocketFD(char* host) {
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
        printf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return -1;
    }

    /* getaddrinfo() returns a list of address structures.
        Try each address until we successfully connect(2).
        If socket(2) (or connect(2)) fails, we (close the socket
        and) try the next address. 
    */
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        sfd = socket(rp->ai_family, rp->ai_socktype,
                    rp->ai_protocol);
        if (sfd == -1)
            continue;
        
        // Set the TCP socket to non-blocking mode
        fcntl(sfd, F_SETFL, O_NONBLOCK);

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */

        close(sfd);
    }

    freeaddrinfo(result);           /* No longer needed */

    if (rp == NULL) {               /* If no address succeeded */
        fprintf(stderr, "Could not connect\n");
        return -1;
    }
    return sfd;
}


// Returns 1 if end of header is reached
int process_header_data(
    int sock,
    struct linkedlist* header_fields, 
    char* line_start, 
    char* line_end, 
    char* buffer, 
    int buffer_len,
    int* inbuf_used
) {
    /* Scan for newlines in the line buffer; we're careful here to deal with embedded \0s
	* an evil server may send, as well as only processing lines that are complete.
	*/
    int keep_going = 1;
    while(keep_going) {
        while ((line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - buffer)))) {
            if(strlen(line_start) == 1 && strcmp(line_start, "\r") == 0) {
                keep_going = 0;
            }
            *line_end = 0; // Set null to divide line
            char* field, data;
            char* divider = memchr(line_start, ':', strlen(line_start)); // tokenise with ':'
            if(divider) {
                *divider = '\0';
                field = line_start; /* Start to ':'  (pointer) */
                data = divider + 2; /* ':' to end without the leading space (pointer) */
            }

            if(data == NULL) {
                printf("Malformed header line: %s %s\n", line_start, divider + 1);
                return 1;
            }
            
            if(strcasecmp(field, "Proxy-Connection") == 0 || strcasecmp(field, "host") == 0)
                continue;

            header_fields->insert(header_fields, field, data);

            line_start = line_end + 1;
        }
        /* Shift buffer down so the unprocessed data is at the start */
        inbuf_used -= (line_start - buffer);
        memmove(buffer, line_start, inbuf_used);

        int rv;
        if((rv = recv(sock, buffer + (*inbuf_used), buffer_len - (*inbuf_used), 0)) <= 0) {
            printf("recv error\n");
            return 1;
        }
        (*inbuf_used) += rv;
    }
    return 0;
}

int send_message(int sfd, char* body, int body_length) {
    while(body_length > 0) {
        int bytes = send(sfd, body, body_length, 0);
        body_length -= bytes;
    }
    if(body_length < 0) return -1;
    return 1;
}