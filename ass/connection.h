#ifndef CONNECTION_H
#define CONNECTION_H

typedef struct request_info {
    int sock;
    char* method;
    char* full_uri;
    char* host;
    char* user_agent;
    char* accept;
    char* content_type;
    int content_length;
} request_info;

// connect to server with CONNECT method
int ConnectMethodServerConnection(int client_sock, char* method, char* absolute_form);

// connect to the server when the method is not CONNECT
// returns 1 if connection = keep-alive
// returns 0 if connectino = closed
int ServerConnection(
    int sock, 
    char* method, 
    char* absolute_form, 
    char* buffer, 
    int buffer_len, 
    int* inbuf_used);

#endif