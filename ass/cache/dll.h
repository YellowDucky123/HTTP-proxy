#ifndef DLL_H
#define DLL_H

typedef struct res {
    char* log;
    int status_code;
    int bytes;
    char* header;
    int h_len;
    char* body;
    int b_len;
} res;

typedef struct Node {

    // To store the Value or data.
    res data;
    char* key;

    // Pointer to point the Previous Element
    struct Node* prev;

    // Pointer to point the Next Element
    struct Node* next;
} Node;

struct Node *insertAtFront(struct Node *head, char* key, res new_data);
struct Node* moveToFront(struct Node* head, struct Node* node);
struct Node* delLast(struct Node *head, char** del_key, int* bytes);
struct Node *delHead(struct Node *head);
struct Node* delPos(struct Node* head, struct Node* node);

#endif