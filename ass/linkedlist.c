#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "linkedlist.h"

struct linkedlist constructor();

char* search_list(struct linkedlist* list, char* key);

void insert_list(struct linkedlist* list, char* key, char* value);

void destroy(struct linkedlist* list);

struct linkedlist linkedListConstructor() {
    struct linkedlist list;
    list.head = NULL;

    list.search = search_list;
    list.insert = insert_list;
    list.destroyList = destroy;
    list.delete = delete_list;

    return list;
}

char* search_list(struct linkedlist* list, char* key) {
    for (node* it = list->head; it != NULL; it = it->next) {
        if(strcasecmp(it->key, key) == 0) {
            return it->value;
        }
    }

    return NULL;
}

void insert_list(struct linkedlist* list, char* key, char* value) {
    node* new_node = malloc(sizeof(node));
    new_node->key = strdup(key);
    new_node->value = strdup(value);
    new_node->next = NULL;

    if (list->head == NULL) {
        list->head = new_node;
        return;
    }

    node* it = list->head;
    while (it != NULL) {
        if (strcasecmp(it->key, key) == 0) {
            free(it->value);
            it->value = strdup(value);
            return;
        }

        if (it->next == NULL) 
            break;
        it = it->next;
    }
    
    it->next = new_node;
}

void destroy(struct linkedlist* list) {
    node* it = list->head;
    while (it != NULL) {
        node* tmp = it->next;
        free(it->key);     // Free duplicated key string
        free(it->value);   // Free duplicated value string
        free(it);          // Free the node itself
        it = tmp;
    }
    list->head = NULL;
}

void delete_list(struct linkedlist* list, char* key) {
    if (!list || !list->head) return; 

    if (strcasecmp(list->head->key, key) == 0) {
        node* tmp = list->head->next;
        free(list->head->key);
        free(list->head->value);
        free(list->head);
        list->head = tmp;
        return;
    }

    node* prev = NULL;
    for (node* it = list->head->next; it != NULL; it = it->next) {
        if(strcasecmp(it->key, key) == 0) {
            node* tmp = it->next;
            free(it->key);
            free(it->value);
            free(it);
            prev->next = tmp;
            return;
        }
        prev = it;
    }
}