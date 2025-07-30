#ifndef CACHE_H
#define CACHE_H

typedef struct node {
    char* key;
    char* value;
    struct node* right;
    struct node* left;
} node;

#endif 