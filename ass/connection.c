#include "connection.h"
#include <netdb.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "linkedlist.h"

char* proxy_header_factory(struct linkedlist* header_fields);
char* process_body(struct linkedlist* header_fields, int sock);
void process_data(struct linkedlist* header_fields, int sock, char* method, char* absolute_form, char* line_start, char* line_end, char* buffer, int buffer_len, int* inbuf_used);
int process_buffer_data(struct linkedlist* header_fields, char* line_start,	char* line_end,	char* buffer, int* inbuf_used);


int ServerConnection(int sock, char* method, char* absolute_form, char* line_start, char* line_end, char* buffer, int buffer_len, int* inbuf_used) {
    struct linkedlist header_fields = linkedListConstructor();

    process_data(&header_fields, sock, method, absolute_form, line_start, line_end, buffer, buffer_len, inbuf_used);


    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    struct addrinfo *result, *rp;
    int sfd;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = 0;

    char* host = header_fields.search(&header_fields, "Host");

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

    if (rp == NULL) {               /* If no address succeeded */
        fprintf(stderr, "Could not connect\n");
        return 1;
    }

    char* proxy_header = proxy_header_factory(&header_fields);

    char* body = process_body(&header_fields, sock);

    send(sock, proxy_header, strlen(proxy_header), 0);
    if(body) {
        send(sock, body, strlen(body), 0);
    }
    
    return 0;
}


// Process data for any header that's not a CONNECT method
void process_data(struct linkedlist* header_fields, int sock, char* method, char* absolute_form, char* line_start, char* line_end, char* buffer, int buffer_len, int* inbuf_used) {    
    /* Gets hostname and request from absolute-form uri */
    char* possible_backup_hostname;
    char* request_instruction;
    if(strncasecmp(absolute_form, "http://", 7) == 0) {
        char* host_start = absolute_form + 7;
        char* path_start = strchr(host_start, '/');
        
        if (path_start) {
            // Temporarily null-terminate hostname
            *path_start = '\0';
            possible_backup_hostname = host_start;

            // Path_start now points to the path starting with '/'
            request_instruction = path_start + 1; // Skip '/'
            
            // If you want to restore the '/' character after processing
            *path_start = '/';
        } else {
            // No path: entire rest is hostname, path = "/"
            possible_backup_hostname = host_start;
            request_instruction = "/";
        }
    }
    else {
        possible_backup_hostname = NULL;
        request_instruction = absolute_form;
    }

    char top[1024];
    sprintf(top, "%s %s HTTP/1.1\r\n", method, request_instruction);
    header_fields->insert(header_fields, "top", top);

    if(possible_backup_hostname != NULL) {
        header_fields->insert(header_fields, "Host", possible_backup_hostname);
    }

    /* Only read until end of header "\r\n" */
    while(process_buffer_data(header_fields, line_start, line_end, buffer, inbuf_used) == 0) {
        int rv;
        if((rv = recv(sock, buffer + (*inbuf_used), buffer_len - (*inbuf_used), 0)) <= 0) {
            printf("recv error\n");
            return 1;
        }
        (*inbuf_used) += rv;
    }
}

// Returns 1 if end of header is reached
int process_buffer_data(struct linkedlist* header_fields, char* line_start, char* line_end, char* buffer, int* inbuf_used) {
    /* Scan for newlines in the line buffer; we're careful here to deal with embedded \0s
	* an evil server may send, as well as only processing lines that are complete.
	*/
	while ((line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - buffer)))) {
        if(strlen(line_start) == 1 && strcmp(line_start, "\r") == 0) {
            return 1;
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
			printf("Malformed header line: %s\n", line_start);
			return;
		}
        
        if(strcasecmp(field, "Proxy-Connection" == 0))
            continue;

        header_fields->insert(header_fields, field, data);

		line_start = line_end + 1;
	}
	/* Shift buffer down so the unprocessed data is at the start */
	inbuf_used -= (line_start - buffer);
	memmove(buffer, line_start, inbuf_used);
    return 0;
}

/* To make the new proxy request header */
char* proxy_header_factory(struct linkedlist* header_fields) {
    header_fields->insert(header_fields, "Connection", "close");
    header_fields->insert(header_fields, "Via", "1.1 z5489321");

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
    offset = sprintf(proxy_header, "%s\r\n", top);
    header_fields->delete(header_fields, "top");

    for(node* it = header_fields->head; it != NULL; it = it->next) {
        int written = sprintf(proxy_header + offset, "%s: %s\r\n", it->key, it->value);
        offset += written;
    }
    sprintf(proxy_header + offset, "\r\n");

    return proxy_header;
}

// to process the request body
char* process_body(struct linkedlist* header_fields, int sock) {
    char* str_content_length = header_fields->search(header_fields, "Content-Length");
    if(str_content_length == NULL) {
        return NULL;
    }

    int content_length = atoi(str_content_length);

    int buffer_len = content_length + 10;
    char* buffer = malloc(buffer_len);

    int accumulate_byte = 0;
    while(accumulate_byte < content_length) {
        int rv;
        if((rv = recv(sock, buffer, buffer_len, 0)) <= 0) {
            printf("recv error\n");
            return 1;
        }
        accumulate_byte += rv;
    }
    return buffer;
}
