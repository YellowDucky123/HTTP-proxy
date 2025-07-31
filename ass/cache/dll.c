#include "dll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void delNode(struct Node* node);

// Function to create a new node
Node *createNode(char* key, res data) {
    struct Node *new_node = malloc(sizeof(struct Node));
    new_node->data = data;
    new_node->key = strdup(key);
    new_node->next = NULL;
    new_node->prev = NULL;
    return new_node;
}

// Function to insert a new node at the front of doubly linked list
struct Node *insertAtFront(struct Node *head, char* key, res new_data) {

    // Create a new node
    struct Node *new_node = createNode(key, new_data);

    // Make next of new node as head
    new_node->next = head;

    // Change prev of head node to new node
    if (head != NULL) {
        head->prev = new_node;
    }

    // Return the new node as the head of the doubly linked list
    return new_node;
}

struct Node* moveToFront(struct Node* head, struct Node* node) {
    if(node == NULL) return NULL;
    if(node->prev == NULL) {
        return node;
    }
    node->prev->next = node->next;

    if(node->next != NULL) node->next->prev = node->prev;

    node->prev = NULL;
    node->next = head;

    return node;
}

// Function to delete the last node of the doubly linked list
struct Node* delLast(struct Node *head, char** del_key, int* bytes) {
  
    // Corner cases
    if (head == NULL) {
        *del_key = NULL;
        *bytes = 0;
        return NULL;
    }
    if (head->next == NULL) {
        *del_key = head->key;
        *bytes = head->data.bytes;
        delNode(head);
        return NULL;
    }

    // Traverse to the last node
    struct Node *curr = head;
    while (curr->next != NULL)
        curr = curr->next;

    *bytes = curr->data.bytes;

    // Update the previous node's next pointer
    curr->prev->next = NULL;

    // Delete the last node
    *del_key = curr->key;
    delNode(curr);

    // Return the updated head
    return head;
}

// Function to delete the first node (head) of the 
// list and return the second node as the new head
struct Node *delHead(struct Node *head) {
  
    // If empty, return NULL
    if (head == NULL)
        return NULL;

    // Store in temp for deletion later
    struct Node *temp = head;

    // Move head to the next node
    head = head->next;

    // Set prev of the new head
    if (head != NULL)
        head->prev = NULL;

    // Free memory and return new head
    free(temp->key);
    delNode(temp);
    return head;
}

// Function to delete a node at a specific position 
// in the doubly linked list
struct Node* delPos(struct Node* head, struct Node* node) {
    if(node->prev == NULL) {
        return delHead(node);
    }

    node->prev->next = node->next;
    node->next->prev = node->prev;
    free(node->key);
    delNode(node);
    return head;
}

void delNode(struct Node* node) {
    free(node->data.body);
    free(node->data.header);
    free(node);
}