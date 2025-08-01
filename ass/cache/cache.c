#include "cache.h"
#include "../util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char* normalised(char* form);

struct cache* cache_construct(int size, int max_object_size) {
    struct cache* cache = malloc(sizeof(struct cache));
    cache->capacity = size;
    cache->curr_size = 0;
    cache->max_object_size = max_object_size;
    cache->ht = ht_construct(size, max_object_size);
    cache->dll = NULL;

    return cache;
}

int delete_cache_LRU(struct cache* cache) {
    printf("--> Removing Last used cache\n");

    char* del_key;
    int bytes;
    cache->dll = delLast(cache->dll, &del_key, &bytes);
    ht_delete(cache->ht, del_key);
    printf("---> removing bytes: %d\n", bytes);
    cache->curr_size -= bytes;
    return 0;
}

int insert_cache(struct cache* cache, char* request_line, 
    int status_code, char* form, char* header, int header_len, char* body, int body_len) {
    printf("----> Inserting into cache %d bytes\n", body_len + header_len); 

    if(body_len + header_len > cache->capacity) {
        printf("--> Object bigger than cache capacity, not caching\n");
        return 0;
    }

    while(cache->curr_size + body_len + header_len > cache->capacity) {
        printf("--> Current cache size: %d", cache->curr_size);
        printf("--> Cache full, deleting LRU\n");
        delete_cache_LRU(cache);
    }

    char* form2 = strdup(form);
    char* key = normalised(form2);
    free(form2);

    res r;
    r.log = strdup(request_line);
    r.bytes = body_len + header_len;

    if(r.bytes > cache->max_object_size) {
        printf("--> Object too large for cache, not caching\n");
        free(key);
        free(r.log);
        return 0;
    }

    r.status_code = status_code;
    r.b_len = body_len;
    r.h_len = header_len;

    if(body != NULL) {
        r.body = malloc(body_len);
        memcpy(r.body, body, body_len);
    } else {
        r.body = NULL;
    }
    
    r.header = malloc(header_len);
    memcpy(r.header, header, header_len);
    

    printf("cache size %d\n", cache->curr_size);
    cache->dll = insertAtFront(cache->dll, key, r);
    ht_insert(cache->ht, key, cache->dll);
    cache->curr_size += r.bytes;
    printf("cache size %d\n", cache->curr_size);
    return 1;
}

res* get_cache(struct cache* cache, char* form) {
    char* form2 = strdup(form);
    char* key = normalised(form2);
    free(form2);

    struct Node* node = ht_get(cache->ht, key);
    if(node == NULL) return NULL;
    printf("here\n");
    cache->dll = moveToFront(cache->dll, node);
    printf("11\n");

    return &(node->data);
}

char* normalised(char* form) {
    char* hostname;
    char* request;
    absoluteform_parser(form, &hostname, &request);

    printf("---> normalising form\n");

    char* colon = strchr(hostname, ':');

    if(colon != NULL) {
        int port = atoi(colon + 1);
        if(port == 80) {
            *colon = 0;
        }
    }

    int h_len = strlen(hostname);
    int r_len = strlen(request);
    for(int i = 0; i < h_len; i++) {
        hostname[i] = tolower(hostname[i]);
    }

    int len = h_len + r_len + 16;
    char* normal = malloc(len);
    snprintf(normal, len, "%s%s", hostname, request);
    printf("---> form normalised\n");
    return normal;
}