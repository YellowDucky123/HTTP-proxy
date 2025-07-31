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
#include <sys/select.h>
#include <pthread.h>	// multi thread

// custom includes
#include "linkedlist.h"
#include "util.h"
#include "cache/cache.h"



// 
#define HEAD 1
#define GET 2
#define POST 3
#define CONNECT 4

char* proxy_header_factory(struct linkedlist* header_fields);
int process_body(struct linkedlist* header_fields, int sock, char** body_buffer, char* buffer, int* inbuf, int* req_body_length);
int process_request_header(
    struct linkedlist* header_fields, 
    int sock, char* method, 
    char* absolute_form, 
    char* buffer, 
    int buffer_len, 
    int* inbuf_used
);

char* responseHeader(int sock, char** buf, 
    int* buf_left, int* status_code, int conn_persistence, struct linkedlist* response_fields, int *final_header_len);
int responseBody(int sock, char* buf, int inbuf, struct linkedlist* response_fields, int* request_method, int* status_code, int* body_length, char** body);
int requestMethodWord(char* method); 
int transferEncoding(int sfd, int client_socket, char* buf, int inbuf, char** f_body, int *final_body_len);

int ConnectTunnel(int client_sock, char* absolute_form) {
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
        printf(">ERROR: Port number not supported for CONNECT HTTPS Tunnel, only supports 443\n");
        send(client_sock, error, strlen(error), 0);
        return -1;
    }

    printf("> establishing CONNECT tunnel to %s:%d\n", host, port);

    int sfd = getSocketFD(host, "https"); // Connect to server
    if(sfd == -1) {
        printf(">ERROR: Tunnel failed\n");
        send(client_sock, error, strlen(error), 0);
        return -1;
    }

    char res[50];
    sprintf(res, 
        "HTTP/1.1 200 Connection Established\r\n\r\n"
    );
    send(client_sock, res, strlen(res), 0);  // send 200 response to client
    printf("--> CONNECT tunnel to %s:%d - Established\n", host, port);

    return sfd;
}

int ConnectMethodServerConnection(int client_sock, int sfd) {
    printf("--> Using CONNECT Tunnel to send request\n");

    int maxfd = ((sfd > client_sock) ? sfd : client_sock) + 1;

    fd_set read_fds;

    while(1) {
        int buf_len = 4096;
        char buffer[buf_len];

        FD_ZERO(&read_fds);
        FD_SET(client_sock, &read_fds);
        FD_SET(sfd, &read_fds);

        int sock_select = select(maxfd, &read_fds, NULL, NULL, NULL);
        if(sock_select < 0) {
            printf("ERROR: sock select failed\n");
            return -1;
        }

        if(FD_ISSET(client_sock, &read_fds)) {
            int rv = recv(client_sock, buffer, buf_len, 0);
            if(rv < 0) {
                perror("recv CONNECT fail");
                close(sfd);
                return -1;                
            }

            if(rv == 0) {
                printf("----> Client disconnected from Tunnel\n");
                break;
            }
            // send(sfd, buffer, rv, 0);
            send_message(sfd, buffer, rv);
            printf("----> CONNECT tunnelled client -> Server\n");
        }

        if(FD_ISSET(sfd, &read_fds)) {
            int rv = recv(sfd, buffer, buf_len, 0);
            if(rv < 0) {
                perror("recv CONNECT fail");
                close(sfd);
                return -1;                
            }

            if(rv == 0) {
                printf("----> Server disconnected from Tunnel\n");
                break;
            }
            // send(sfd, buffer, rv, 0);
            send_message(client_sock, buffer, rv);
            printf("----> CONNECT tunnelled Server -> Client\n");
        }
    }
    close(sfd);
    return 0;
}

int ServerConnection(int sock, char* method, char* absolute_form, char* buffer, int buffer_len, 
    int* inbuf_used, cache* cache, int* stat_code, int* bytes, char* request_line, pthread_mutex_t* stats_lock) {
    struct linkedlist header_fields = linkedListConstructor();
    int requestMethod = requestMethodWord(method);

    int validity = process_request_header(&header_fields, sock, method, absolute_form, buffer, buffer_len, inbuf_used);
    printf("data processed\n");
    if(validity == -1) {
        printf("data not valid\n");
        return -1;
    }

    /* Connection status: keep-alive / close */
    int conn_exist = 1;
    char* conn_status_str = header_fields.search(&header_fields, "connection");
    if(conn_status_str == NULL) {
        conn_status_str = header_fields.search(&header_fields, "proxy-connection");
        /* conn_status_str is still NULL, then both connection and proxy-connection does not exist */
        if(conn_status_str == NULL) {
            conn_exist = 0;
        }
    }

    printf("> checking whether client wants to keep-alive client-proxy connection\n");
    int conn_status = 1;
    if(conn_exist) {
        conn_status = (strcasecmp(conn_status_str, "close") == 0) ? 0 : 1;
    } 
    
    char* host = header_fields.search(&header_fields, "Host");

    int sfd = getSocketFD(host, "http");    // Connect to host
    if(sfd == -1) {
        printf(">ERROR: socket not found\n");
        return -1;
    }
    printf("-> Proxy connected to server\n");

    char* proxy_header;
    int req_body_length;
    char* body;
    int rs = proxyMessageSend(&proxy_header, &body, &header_fields, sfd, buffer, inbuf_used, &req_body_length);
    if(rs == -1) {
        return -1;
    }

    send(sfd, proxy_header, strlen(proxy_header), 0);
    printf("proxy sent request header -\n%s - to server: %s\n", proxy_header, host);
    free(proxy_header);
    if(body) {
        send_message(sfd, body, req_body_length);
        free(body);
        printf("proxy sent request body to server: %s\n", host);
    }

//-------------------------------------------------------------------------------------------

    int header_len;
    struct linkedlist response_fields = linkedListConstructor();
    int status_code;
    int inbuf;
    char* buf;
    char* response_header = responseHeader(sfd, &buf, &inbuf, &status_code, conn_status, &response_fields, &header_len);
    *stat_code = status_code;

    printf("\nresponse header -\n%s\n", response_header);

    send_message(sock, response_header, strlen(response_header));

    /* IF CHUNKED ENCODING */
    if(isChunkedTransferEncoding(&response_fields)) {
        int body_len;
        char* final_body;
        int ret = conn_status;
        if(transferEncoding(sfd, sock, buf, inbuf, &final_body, &body_len) == - 1) {
            ret = -1;
        }

        *bytes = body_len + header_len;

        if(requestMethod == GET) {
            pthread_mutex_lock(stats_lock);
            insert_cache(cache, request_line, status_code, absolute_form, response_header, header_len, final_body, body_len);
            pthread_mutex_unlock(stats_lock);
        }

        free(final_body);
        free(response_header);

        printf("-> Transfer-Encoding finished\n");
        
        /* clearing up the fields */
        close(sfd);
        header_fields.destroyList(&header_fields);
        response_fields.destroyList(&response_fields);

        return ret;
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

    if(response_body) {
        send_message(sock, response_body, body_length);
    }

    *bytes = body_length + header_len;

    if(requestMethod == GET) {
        pthread_mutex_lock(stats_lock);
        insert_cache(cache, request_line, status_code, absolute_form, response_header, header_len, response_body, body_length);
        pthread_mutex_unlock(stats_lock);
    }

    free(response_body);
    free(response_header);
    
    /* clearing up the fields */
    close(sfd);
    header_fields.destroyList(&header_fields);
    response_fields.destroyList(&response_fields);

    return conn_status;
}

/* transfer encoding logic, if return 0, something is weird */
int transferEncoding(int sfd, int client_socket, char* buf, int inbuf, char** f_body, int *final_body_len) {
    printf("> Start transfer encoding\n");

    int body_len = 0;
    int body_size = 2048;
    char* final_body = malloc(body_size);

    if(inbuf > 0) {
        int a = appendToBuffer(&final_body, &body_len, &body_size, buf, inbuf);
        if(a != 1) {
            printf("ERROR: Transfer Encoding error\n");
            return -1;
        }
    }

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
            break;
        }

        int a = appendToBuffer(&final_body, &body_len, &body_size, buffer, rv);
        if(a != 1) {
            printf("ERROR: Transfer Encoding error\n");
            return -1;
        }
    }

    send_message(client_socket, final_body, body_len);
    *f_body = final_body;
    *final_body_len = body_len;

    return 1;
}

/* Receive response from server and forward to client */
char* responseHeader(int sock, char** buf, 
    int* buf_left, int* status_code, int conn_persistence, struct linkedlist* response_fields, int *final_header_len) {    
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

    if(conn_persistence) {
        response_fields->insert(response_fields, "Connection", "keep-alive");
        printf("-> Client connection: keep-alive\n");
    } else {
        response_fields->insert(response_fields, "Connection", "close");
        printf("-> Client connection: close\n");
    }
 
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

    *final_header_len = offset + 2;

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

    if(inbuf > 0) memmove(*body, buf, inbuf); // if there's any data in buf

    if(recv_message(sock, *body + inbuf, content_length - inbuf) == -1) {
        return -1;
    }

    return 1;
}

// Process data for any header that's not a CONNECT method
int process_request_header(
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

    absoluteform_parser(absolute_form, &possible_backup_hostname, &request_instruction);

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