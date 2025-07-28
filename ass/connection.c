#include "connection.h"

// includes
#include <netdb.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <time.h>
#include <stdlib.h>

// custom includes
#include "linkedlist.h"
#include "util.h"

// 
#define HEAD 1
#define GET 2
#define POST 3
#define CONNECT 4

char* proxy_header_factory(struct linkedlist* header_fields);
char* process_body(struct linkedlist* header_fields, int sock, int* req_body_length);
int process_data(
    struct linkedlist* header_fields, 
    int sock, char* method, 
    char* absolute_form, 
    char* buffer, 
    int buffer_len, 
    int* inbuf_used
);

char* responseHeader(int sock, char** buf, int* buf_left, int* status_code, struct linkedlist* response_fields);
int responseBody(int sock, char* buf, int inbuf, struct linkedlist* response_fields, int* request_method, int* status_code, int* body_length, char** body);
int requestMethodWord(char* method); 
int transferEncoding(int sfd, int client_socket);

int Connect(int client_sock, char* absolute_form) {
    char* colon = strchr(absolute_form, ':');
    *colon = 0; // NULL

    time_t currentTime;
    time(&currentTime);
    char* host = absolute_form;
    int port = atoi(colon + 1);

    /* Error */
    char error[200];
    sprintf(error, 
        "HTTP/1.1 400 Bad Request\r\n"
        "Server: Proxy-Kelvin\r\n"
        "Date: %s\r\n"
        "Content-Type: text/html\r\n\r\n"
        "<h1>invalid port</h1>",
        ctime(&currentTime)
    );
    if(port != 443) {
        write(client_sock, error, strlen(error));
        return -1;
    }

    printf("> establishing CONNECT tunnel to %s:%d\n", host, port);

    int sfd = getSocketFD(host); // Connect to server
    if(sfd == -1) {
        write(client_sock, error, strlen(error));
        return -1;
    }

    char res[50];
    sprintf(res, 
        "HTTP/1.1 200 Connection Established\r\n\r\n"
    );
    write(client_sock, res, strlen(res));  // send 200 response to client

    return sfd;
}

int ConnectMethodServerConnection(int client_sock, int sfd) {
    printf(">-- Using CONNECT Tunnel to send request\n");

    int maxfd = ((sfd > client_sock) ? sfd : client_sock) + 1;

    fd_set read_fds;

    while(1) {
        char buffer[4096];

        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        FD_SET(sfd, &read_fds);

        int sock_select = select(maxfd, &read_fds, NULL, NULL, NULL);
        if(sock_select < 0) {
            printf("ERROR: sock select failed\n");
            return -1;
        }

        if(FD_ISSET(client_sock, &read_fds)) {
            int rv = recv(client_sock, buffer, 2048, 0);
            if(rv < 0) {
                perror("recv CONNECT fail");
                close(sfd);
                return -1;                
            }

            if(rv == 0) break;

            send_message(sfd, buffer, rv);
        }

        if(FD_ISSET(sfd, &read_fds)) {
            int rv = recv(sfd, buffer, 2048, 0);
            if(rv < 0) {
                perror("recv CONNECT fail");
                close(sfd);
                return -1;                
            }

            if(rv == 0) break;

            send_message(client_sock, buffer, rv);
        }
    }
    close(sfd);
    return 0;
}

int ServerConnection(int sock, char* method, char* absolute_form, char* buffer, int buffer_len, int* inbuf_used) {
    struct linkedlist header_fields = linkedListConstructor();
    int requestMethod = requestMethodWord(method);

    int validity = process_data(&header_fields, sock, method, absolute_form, buffer, buffer_len, inbuf_used);
    printf("data processed\n");
    if(validity == -1) {
        printf("data not valid\n");
        return -1;
    }

    /* Connection status: keep-alive / close */
    char* conn_status_str = header_fields.search(&header_fields, "connection");
    if(conn_status_str == NULL) {
        conn_status_str = header_fields.search(&header_fields, "proxy-connection");
        /* conn_status_str is still NULL, then both connection and proxy-connection does not exist */
        if(conn_status_str == NULL) {
            printf("connection/proxy-connection field does not exist in request header\n");
            return -1;
        }
    }

    printf("> checking whether client wants to keep-alive client-proxy connection\n");
    int conn_status = (strcasecmp(conn_status_str, "close") == 0) ? 0 : 1;

    char* host = header_fields.search(&header_fields, "Host");

    printf("Proxy connected to server\n");
    int sfd = getSocketFD(host);    // Connect to host

    char* proxy_header = proxy_header_factory(&header_fields);

    int req_body_length;
    char* body = process_body(&header_fields, sfd, &req_body_length);

    send(sfd, proxy_header, strlen(proxy_header), 0);
    printf("proxy sent request header -\n%s - to server: %s\n", proxy_header, host);
    free(proxy_header);
    if(body) {
        send_message(sfd, body, req_body_length);
        free(body);
        printf("proxy sent request body to server: %s\n", host);
    }

//-------------------------------------------------------------------------------------------

    struct linkedlist response_fields = linkedListConstructor();
    int status_code;
    int inbuf;
    char* buf;
    char* response_header = responseHeader(sfd, &buf, &inbuf, &status_code, &response_fields);

    printf("\nresponse header -\n%s\n", response_header);

    /* IF CHUNKED ENCODING */
    if(isChunkedTransferEncoding(&response_fields)) {
        if(transferEncoding(sfd, sock) == - 1) {
            /* clearing up the fields */
            close(sfd);
            header_fields.destroyList(&header_fields);
            response_fields.destroyList(&response_fields);

            return -1;
        }
        
        /* clearing up the fields */
        close(sfd);
        header_fields.destroyList(&header_fields);
        response_fields.destroyList(&response_fields);

        return conn_status;
    }

    /* GO HERE IF NOT CHUNKED ENCODING */

    int body_length;
    char* response_body = NULL;
    printf("> Parsing body\n");
    if(responseBody(sfd, buf, inbuf, &response_fields, &requestMethod, &status_code, &body_length, &response_body) == -1) {
        printf("ERROR: Recv failed\n");
        return -1;
    }

    printf("Response body -\n%s\n", response_body);

    write(sock, response_header, strlen(response_header));
    free(response_header);
    if(response_body) {
        send_message(sock, response_body, body_length);
        free(response_body);
    }
    
    /* clearing up the fields */
    close(sfd);
    header_fields.destroyList(&header_fields);
    response_fields.destroyList(&response_fields);

    return conn_status;
}

/* transfer encoding logic, if return 0, something is weird */
int transferEncoding(int sfd, int client_socket) {
    while(1) {
        int buffer_len = 2048;
        char buffer[buffer_len];
        int rv;
        if((rv = recv(sfd, buffer, buffer_len, 0)) < 0) {
            printf("ERROR: transfer-encoding recv failed\n");
            return -1;
        }

        /* If it's 0 then server disconnected */
        if(rv == 0) {
            return 1;
        }

        send_message(client_socket, buffer, rv);
    }

    return 0;
}

/* Receive response from server and forward to client */
char* responseHeader(int sock, char** buf, int* buf_left, int* status_code, struct linkedlist* response_fields) {
    int respond_buffer_len = 1024;
    *buf = malloc(respond_buffer_len);
    char* respond_buffer = *buf;

    int inbuf_used = 0;
    int rv;
    if((rv = recv(sock, respond_buffer, respond_buffer_len, 0)) < 0) {
        printf("ERROR: recv response failed!\n");
    }

    inbuf_used = rv;

    char* line_start = respond_buffer;
    char* line_end;

    /* Parse the response line */
    line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - respond_buffer));
    *line_end = 0;  // NULL

    char* space1 = strchr(line_start, ' ');
    char* space2 = strchr(space1 + 1, ' ');
    *space2 = 0;
    char* status_code_str = strdup(space1 + 1);
    char* status_message = strdup(space2 + 1);

    *status_code = atoi(status_code_str);

    line_start = line_end + 1;
    inbuf_used -= (line_start - respond_buffer);
    memmove(respond_buffer, line_start, inbuf_used);
    respond_buffer[inbuf_used] = 0;

    /* Parse the response headers */
    process_header_data(sock, response_fields, respond_buffer, respond_buffer_len, &inbuf_used);
    *buf_left = inbuf_used;
    response_fields->insert(response_fields, "Via", "1.1 z5489321");
 
    /* Make the header */
    size_t bytes = strlen(status_code_str) + strlen(status_message) + 10 + 5;   // padding bytes
    for(node* it = response_fields->head; it != NULL; it = it->next) {
        bytes += strlen(it->key) + 2 + strlen(it->value) + 2 + 1;
    }

    char* return_buffer = malloc(bytes + 1);

    printf("> status code: %s | status message: %s\n", status_code_str, status_message);

    int offset = sprintf(return_buffer, "HTTP/1.1 %s %s\r\n", status_code_str, status_message);
    for(node* it = response_fields->head; it != NULL; it = it->next) {
        int written = sprintf(return_buffer + offset, "%s: %s\r\n", it->key, it->value);
        offset += written;
    }
    sprintf(return_buffer + offset, "\r\n");

    free(status_code_str);
    free(status_message);
    return return_buffer;
}

int responseBody(int sock, char* buf, int inbuf,
    struct linkedlist* response_fields, int* request_method, int* status_code, int* body_length, char** body) {
    if((*request_method) == HEAD || (*status_code) == 204 || (*status_code) == 304) {
        return 1;
    }

    /* Get content length and check if there is a body */
    int content_length = atoi(response_fields->search(response_fields, "content-length"));
    *body_length = content_length;
    if(content_length == 0) {
        return 1;
    }

    *body = malloc(content_length + 4);

    if(inbuf > 2) memmove(*body, buf, inbuf); // if there's any data in buf

    if(recv_message(sock, *body + inbuf, content_length - inbuf) == -1) {
        return -1;
    }

    return 1;
}

// Process data for any header that's not a CONNECT method
int process_data(
    struct linkedlist* header_fields, 
    int sock, char* method, 
    char* absolute_form, 
    char* buffer, 
    int buffer_len, 
    int* inbuf_used
) {    
    /* Gets hostname and request from absolute-form uri */
    char* possible_backup_hostname;
    char* request_instruction;
    if(strncasecmp(absolute_form, "http://", 7) == 0) {
        char* host_start = absolute_form + 7;
        char* path_start = strchr(host_start, '/');
        
        if (path_start) {
            // Temporarily null-terminate hostname
            *path_start = 0;
            possible_backup_hostname = strdup(host_start);
            printf("-------- %s\n", possible_backup_hostname);

            // restore the '/' character after processing
            *path_start = '/';
            // Path_start now points to the path starting with '/'
            request_instruction = path_start; // Skip '/'
        
        } else {
            // No path: entire rest is hostname, path = "/"
            possible_backup_hostname = strdup(host_start);
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

    printf("hostname - %s\n", possible_backup_hostname);
    printf("request - %s\n", request_instruction);
    if(possible_backup_hostname != NULL) {
        header_fields->insert(header_fields, "Host", possible_backup_hostname);
        free(possible_backup_hostname);
    }

    /* Only read until end of header "\r\n" */
    return process_header_data(sock, header_fields, buffer, buffer_len, inbuf_used);
}


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
char* process_body(struct linkedlist* header_fields, int sock, int* req_body_length) {
    char* str_content_length = header_fields->search(header_fields, "Content-Length");
    if(str_content_length == NULL) {
        return NULL;
    }

    int content_length = atoi(str_content_length);
    *req_body_length = content_length;

    if(content_length == 0) {
        return NULL;
    }

    int buffer_len = content_length + 10;
    char* buffer = malloc(buffer_len);

    int accumulate_byte = 0;
    while(accumulate_byte < content_length) {
        int rv;

        // if 0 then it's disconnected not error
        if((rv = recv(sock, buffer, content_length, 0)) < 0) {
            printf("recv error\n");
            return NULL;
        }
        accumulate_byte += rv;
    }
    return buffer;
}


int requestMethodWord(char* method) {
    if(strcasecmp(method, "HEAD") == 0) {
        return HEAD;
    } else if(strcasecmp(method, "GET") == 0) {
        return GET;
    } else if(strcasecmp(method, "POST") == 0) {
        return POST;
    } else if(strcasecmp(method, "CONNECT") == 0) {
        return CONNECT;
    }
    return -1;
}