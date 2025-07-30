#ifndef UTIL_H
#define UTIL_H

#include "linkedlist.h"

// Connects to a host and returns the socket file descriptor
int getSocketFD(char* host, char* type);

/*
the function will at the end have inbuf_used repurposed for the bytes left inside buffer after header
*/
int process_header_data(
    int sock,
    struct linkedlist* header_fields, 
    char* buffer, 
    int buffer_len,
    int* inbuf_used
);

int send_message(int sfd, char* body, int body_length);

/* 
Takes in a message with recv from a socket

sock = the socket fd
body = the buffer
content_length = the amount of bytes to take in
*/
int recv_message(int sock, char* body, int content_length);

/* Checks whether header has chunked encoding in it */
/* returns true or false (1 or 0) */
int isChunkedTransferEncoding(struct linkedlist* response_fields);

int proxyMessageSend(char** header, char** body, 
    struct linkedlist* header_fields, int sfd, char* buf, int* inbuf_used, int* req_body_length);

int appendToBuffer(char** buf, int* offset, int* buf_size, char* string, int string_size);

void absoluteform_parser(char* absolute_form, char** hostname ,char** request_instruction);

#endif