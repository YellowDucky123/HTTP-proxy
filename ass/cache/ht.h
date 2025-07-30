#ifndef HT_H
#define HT_H
#include "dll.h"

typedef struct ht_t {
    char* key;
    struct Node* node;
} ht_t;

typedef struct ht {
    int size;
    int curr_size;
    int max_object_size;
    ht_t *table;
} ht;

ht* ht_construct(int size, int max_object_size);
int ht_delete(ht* ht, char* key);
struct Node* ht_get(ht* ht, char* key);
char* ht_insert(ht* ht, char* key, struct Node* node);

#endif