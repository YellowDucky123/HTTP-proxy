#ifndef CACHE_H
#define CACHE_H

#include "ht.h"
#include "dll.h"
#include "../util.h"


typedef struct cache {
    int capacity;
    int curr_size;
    int max_object_size;

    ht* ht;
    struct Node* dll;
} cache;

struct cache* cache_construct(int size, int max_object_size);
int delete_cache_LRU(struct cache* cache);
int insert_cache(struct cache* cache, char* request_line, int status_code, char* form, char* header, int header_len, char* body, int body_len);
res* get_cache(struct cache* cache, char* form);

#endif