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
void process_data(
    struct linkedlist* header_fields, 
    int sock, char* method, 
    char* absolute_form, 
    char* line_start, 
    char* line_end, 
    char* buffer, 
    int buffer_len, 
    int* inbuf_used
);

char* responseHeader(int sock, int* status_code, struct linkedlist* response_fields);
char* responseBody(int sock, struct linkedlist* response_fields, int* request_method, int* status_code, int* body_length);
int requestMethodWord(char* method); 

int ConnectMethodServerConnection(int client_sock, char* method, char* absolute_form) {
    if(strcmp(method, "CONNECT") != 0) {
        return -1;
    }

    char* colon = strchr(absolute_form, ':');
    *colon = 0; // NULL

    time_t currentTime;
    time(&currentTime);
    char* host = absolute_form;
    int port = atoi(colon + 1);
    if(port != 443) {
        char res[1024];
        sprintf(res, 
            "HTTP/1.1 400 Bad Request\r\n"
            "Server: Proxy-Kelvin\r\n"
            "Date: %s\r\n"
            "Content-Type: text/html\r\n\r\n"
            "<h1>invalid port</h1>",
            ctime(&currentTime)
        );
        write(client_sock, res, strlen(res));
        return -1;
    }

    int sfd = getSocketFD(host); // Connect to server

    char res[1024];
    sprintf(res, 
        "HTTP/1.1 200 Connection Established\r\n\r\n"
    );
    write(client_sock, res, strlen(res));  // send 200 response to client

    while(1) {
        int rv;

        char buffer[2048];
        if((rv = recv(client_sock, buffer, 2048, 0)) <= 0) {
            if(rv == 0) {
                break;
            }
            perror("recv CONNECT fail");
            return -1;
        }
        send(sfd, buffer, strlen(buffer), 0);

        char response[2048];
        if((rv = recv(sfd, response, 2048, 0)) <= 0) {
            if(rv == 0) {
                break;
            }
            perror("recv CONNECT response failed");
            return -1;
        }
        write(client_sock, response, strlen(response));
    }
    return 0;
}

int ServerConnection(int sock, char* method, char* absolute_form, char* line_start, char* line_end, char* buffer, int buffer_len, int* inbuf_used) {
    struct linkedlist header_fields = linkedListConstructor();
    int requestMethod = requestMethodWord(method);

    process_data(&header_fields, sock, method, absolute_form, line_start, line_end, buffer, buffer_len, inbuf_used);
    
    /* Connection status: keep-alive / close */
    char* conn_status_str = header_fields.search(&header_fields, "connection");
    int conn_status = 1;
    if(strcasecmp(conn_status_str, "close") == 0) {
        conn_status = 0;
    }

    char* host = header_fields.search(&header_fields, "Host");

    int sfd = getSocketFD(host);    // Connect to host

    char* proxy_header = proxy_header_factory(&header_fields);

    int req_body_length;
    char* body = process_body(&header_fields, sfd, &req_body_length);

    send(sfd, proxy_header, strlen(proxy_header), 0);
    free(proxy_header);
    printf("proxy sent request header to server: %s\n", header_fields.search(&header_fields, "host"));
    if(body) {
        send_message(sfd, body, req_body_length);
        free(body);
        printf("proxy sent request body to server: %s\n", header_fields.search(&header_fields, "host"));
    }

    struct linkedlist response_fields = linkedListConstructor();
    int status_code;
    char* response_header = responseHeader(sfd, &status_code, &response_fields);
    int body_length;
    char* response_body = responseBody(sfd, &response_fields, &requestMethod, &status_code, &body_length);

    // 1234567890\r\n
    // asdfghjkljhgfdsadsfgh jgfsdasfdgfhjghgfgdhjgsf

    // 4\r\nlike5\r\nsomet7\r\nsomethn

    write(sock, response_header, strlen(response_header));
    free(response_header);
    if(response_body) {
        // loop through, strlen on body fix
        send_message(sock, response_body, body_length);
        free(response_body);
    }
    
    close(sfd);
    return conn_status;
}

/* Receive response from server and forward to client */
char* responseHeader(int sock, int* status_code, struct linkedlist* response_fields) {
    int respond_buffer_len = 1024;
    char respond_buffer[respond_buffer_len];
    int inbuf_used = 0;
    int rv;
    if((rv = recv(sock, respond_buffer, respond_buffer_len, 0)) < 0) {
        printf("recv response failed!\n");
    }

    inbuf_used = rv;

    char* line_start = respond_buffer;
    char* line_end;

    /* Parse the response line */
    line_end = (char*)memchr((void*)line_start, '\n', inbuf_used - (line_start - respond_buffer));
    *line_end = 0;  // NULL

    char* status_code_str = strtok(line_start, " ");
    status_code_str = strtok(NULL, " ");    // the code
    char* status_message = strtok(NULL, " ");   // the message

    *status_code = atoi(status_code_str);

    line_start = line_end + 1;

    /* Parse the response headers */
    process_header_data(sock, response_fields, line_start, line_end, respond_buffer, respond_buffer_len, &inbuf_used);
    response_fields->insert(response_fields, "Via", "1.1 z5489321");
 
    /* Make the header */
    size_t bytes = strlen(status_code_str) + strlen(status_message) + 10 + 5;   // padding bytes
    for(node* it = response_fields->head; it != NULL; it = it->next) {
        bytes += strlen(it->key) + 2 + strlen(it->value) + 2 + 1;
    }

    char* return_buffer = malloc(bytes + 1);

    int offset = 0;
    int written = sprintf(return_buffer, "HTTP/1.1 %s %s\r\n", status_code_str, status_message);
    for(node* it = response_fields->head; it != NULL; it = it->next) {
        written = sprintf(return_buffer + offset, "%s: %s\r\n", it->key, it->value);
        offset += written;
    }
    sprintf(return_buffer + offset, "\r\n");

    return return_buffer;
}

char* responseBody(int sock, struct linkedlist* response_fields, int* request_method, int* status_code, int* body_length) {
    if((*request_method) == HEAD || (*status_code) == 204 || (*status_code) == 304) {
        return NULL;
    }

    int content_length = atoi(response_fields->search(response_fields, "content-length")) + 4;
    *body_length = content_length;

    char* body = malloc(content_length);

    int acumulate_byte = 0;
    while(acumulate_byte < content_length) {
        int rv;
        if(recv(sock, body, content_length, 0) <= 0) {
            printf("read response body fail\n");
            exit(1);
        }
        acumulate_byte += rv;
    }

    return body;
}

// Process data for any header that's not a CONNECT method
void process_data(
    struct linkedlist* header_fields, 
    int sock, char* method, 
    char* absolute_form, 
    char* line_start, 
    char* line_end, 
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
    process_header_data(sock, header_fields, line_start, line_end, buffer, buffer_len, inbuf_used);
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

// to process the request body returns buffer containing body
char* process_body(struct linkedlist* header_fields, int sock, int* req_body_length) {
    char* str_content_length = header_fields->search(header_fields, "Content-Length");
    if(str_content_length == NULL) {
        return NULL;
    }

    int content_length = atoi(str_content_length);
    *req_body_length = content_length;

    int buffer_len = content_length + 10;
    char* buffer = malloc(buffer_len);

    int accumulate_byte = 0;
    while(accumulate_byte < content_length) {
        int rv;

        // if 0 then it's disconnected not error
        if((rv = recv(sock, buffer, buffer_len, 0)) < 0) {
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