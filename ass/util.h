#ifndef UTIL_H
#define UTIL_H

// Connects to a host and returns the socket file descriptor
int getSocketFD(char* host);

int process_header_data(
    int sock,
    struct linkedlist* header_fields, 
    char* buffer, 
    int buffer_len,
    int* inbuf_used
);

int send_message(int sfd, char* body, int body_length);

#endif