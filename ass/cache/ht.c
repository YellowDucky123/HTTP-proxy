#include "ht.h"
#include "dll.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static uint64_t hash_key(const char* key);

#define FNV_OFFSET 14695981039346656037UL
#define FNV_PRIME 1099511628211UL

ht* ht_construct(int size, int max_object_size) {
    ht* ht = malloc(sizeof(ht));
    ht->curr_size = 0;
    ht->max_object_size = max_object_size;
    ht->size = size;
    ht->table = calloc(size, sizeof(ht_t));
    return ht;
}

int ht_delete(ht* ht, char* key) {
    // AND hash with capacity-1 to ensure it's within entries array.
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(ht->size - 1));

    // Loop till we find an empty entry
    size_t start = index;
    while (ht->table[index].key != NULL) {
        if (strcmp(key, ht->table[index].key) == 0) {
            // Found key, return value.
            ht->table[index].node = NULL;
            return 0;
        }
        // Key wasn't in this slot, move to next (linear probing).
        index++;
        if (index == start) return -1;
        if (index >= ht->size) {
            // At end of entries array, wrap around.
            index = 0;
        }
    }
    return 1;
}

struct Node* ht_get(ht* ht, char* key) {
    // AND hash with capacity-1 to ensure it's within entries array.
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(ht->size - 1));
    printf("index htget: %ld\n", index);
    // Loop till we find an empty entry

    size_t start = index;
    while (ht->table[index].key != NULL) {
        if (strcmp(key, ht->table[index].key) == 0) {
            // Found key, return value.
            return ht->table[index].node;
        }

        // Key wasn't in this slot, move to next (linear probing).
        index++;
        if (index == start) break;
        if (index >= ht->size) {
            // At end of entries array, wrap around.
            index = 0;
        }
    }
    return NULL;
}

char* ht_insert(ht* ht, char* key, struct Node* node) {
    uint64_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint64_t)(ht->size - 1));

    // Loop till we find an empty entry.
    while (ht->table[index].key != NULL) {
        if (strcmp(key, ht->table[index].key) == 0) {
            // Found key (it already exists), update value.
            ht->table[index].node = node;
            return ht->table[index].key;
        }
        // Key wasn't in this slot, move to next (linear probing).
        index++;
        if (index >= ht->size) {
            // At end of ht->table array, wrap around.
            index = 0;
        }
    }
    
    // Didn't find key, allocate+copy if needed, then insert it.
    char* k = strdup(key);
    if (k == NULL) {
        return NULL;
    }
    (ht->curr_size)++;

    printf("index ht: %ld\n", index);
    ht->table[index].key = k;
    ht->table[index].node = node;
    return k;
}

// Return 64-bit FNV-1a hash for key (NUL-terminated). See description:
// https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function
static uint64_t hash_key(const char* key) {
    uint64_t hash = FNV_OFFSET;
    for (const char* p = key; *p; p++) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}