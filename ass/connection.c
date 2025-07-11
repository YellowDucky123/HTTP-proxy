#include "connection.h"
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

void process_data(request_info* req_info, char* line_start,	char* line_end,	char* buffer, int* inbuf_used);

void process_buffer_data(int* header_line_at, request_info* req_info, char* line_start,	char* line_end,	char* buffer, int* inbuf_used);

char* proxy_header_factory(request_info* req_info);


int ServerConnection(request_info* req_info, char* line_start, char* line_end, char* buffer, int* inbuf_used) {
    process_data(req_info, line_start, line_end, buffer, inbuf_used);

    struct addrinfo hints = {0};
    struct addrinfo *result, *rp;
    int sfd;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    int s = getaddrinfo(host, "http", &hints, &result);
    if (s != 0) {
        printf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
        return 1;
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

        if (connect(sfd, rp->ai_addr, rp->ai_addrlen) != -1)
            break;                  /* Success */

        close(sfd);
    }

    freeaddrinfo(result);           /* No longer needed */

    if (rp == NULL) {               /* No address succeeded */
        fprintf(stderr, "Could not connect\n");
        return 1;
    }

    char* proxy_header = proxy_header_factory(req_info);

    
}

char* proxy_header_factory(request_info* req_info) {
    int len = snprintf(NULL, 0, 
    "%s %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: %s\r\n"
    "Accept: %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %s\r\n\r\n",
    method, uri, host, user_agent, accept, content_type, content_length);  // get size needed

    char* proxy_header = malloc(len + 1); 

    snprintf(proxy_header, sizeof(proxy_header),
    "%s %s HTTP/1.1\r\n"
    "Host: %s\r\n"
    "User-Agent: %s\r\n"
    "Accept: %s\r\n"
    "Content-Type: %s\r\n"
    "Content-Length: %s\r\n\r\n",
    method, uri, host, user_agent, accept, content_type, content_length); 

    return proxy_header;
}

// process data for any header that's not a CONNECT method
void process_data(request_info* req_info, char* line_start,	char* line_end,	char* buffer, int* inbuf_used) {
    int header_line_at = 1;
    while(header_line_at <= 5) {
        process_buffer_data(&header_line_at, req_info, line_start, line_end, buffer, inbuf_used);
    }
}

void process_buffer_data(int* header_line_at, request_info* req_info, char* line_start,	char* line_end,	char* buffer, int* inbuf_used) {
    /* Scan for newlines in the line buffer; we're careful here to deal with embedded \0s
	* an evil server may send, as well as only processing lines that are complete.
	*/
	while ((line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - inbuf)))) {
		*line_end = 0;
		char* data = strtok(buffer, " ");
		data = strtok(NULL, " ");
		if(data == NULL) {
			printf("failed to parse data\n");
			return 1;
		}

		switch(*header_line_at) {
			case 1:
				strcpy(req_info->host, data);
				break;
			case 2:
				strcpy(req_info->user_agent, data);
				break;
			case 3:
				strcpy(req_info->accept, data);
				break;
			case 4:
				strcpy(req_info->content_type, data);
				break;
			case 5:
				strcpy(req_info->content_length, data);
		}
		line_start = line_end + 1;
        *header_line_at++
	}
	/* Shift buffer down so the unprocessed data is at the start */
	inbuf_used -= (line_start - buffer);
	memmove(buffer, line_start, inbuf_used);
}