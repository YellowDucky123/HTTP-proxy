#ifndef LINKEDLIST_H
#define LINKEDLIST_H

typedef struct node {
    char* key;
    char* value;
    struct node* next;
} node;

struct linkedlist {
    node* head;

    char* (*search)(struct linkedlist* list, char* key);

    void (*insert)(struct linkedlist* list, char* key, char* value);

    void (*destroyList)(struct linkedlist* list);

    void (*delete)(struct linkedlist* list, char* key);
};

struct linkedlist linkedListConstructor();

char* search_list(struct linkedlist* list, char* key);

void insert_list(struct linkedlist* list, char* key, char* value);

void delete_list(struct linkedlist* list, char* key);

#endif