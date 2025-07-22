#ifndef UTIL_H
#define UTIL_H

// Connects to a host and returns the socket file descriptor
int getSocketFD(char* host);

int process_header_data(
    int sock,
    struct linkedlist* header_fields, 
    char* line_start, 
    char* line_end, 
    char* buffer, 
    int buffer_len,
    int* inbuf_used
); 

#endif