#ifndef CONNECTION_H
#define CONNECTION_H

#include "cache/cache.h"
#include "util.h"

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

int ConnectTunnel(int client_sock, char* absolute_form);

// used to tunnel after CONNECT has been established
// returns -1 if ERROR
int ConnectMethodServerConnection(int client_sock, int sfd);

// connect to the server when the method is not CONNECT
// returns 1 if connection = keep-alive
// returns 0 if connectino = closed
int ServerConnection(
    int sock, 
    char* method, 
    char* absolute_form, 
    char* buffer, 
    int buffer_len, 
    int* inbuf_used,
    cache* cache);

#endif