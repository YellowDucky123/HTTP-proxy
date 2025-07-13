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
int ConnectMethodServerConnection();

// connect to the server when the method is not CONNECT
int ServerConnection(request_info* req_info, char* line_start, char* line_end, char* buffer, int inbuf_used);

#endif