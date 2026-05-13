#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct HashEntry {
    void *key;
    void *value;
    struct HashEntry *next;
} HashEntry;

typedef size_t (*HashFunc)(const void *key);
typedef int (*KeyEqFunc)(const void *a, const void *b);
typedef void (*DestroyFunc)(void *data);

typedef struct {
    HashEntry **buckets;
    size_t num_buckets;
    size_t key_size;
    size_t value_size;
    size_t size;
    HashFunc hash;
    KeyEqFunc key_eq;
    DestroyFunc key_destroy;
    DestroyFunc value_destroy;
} HashTable;

HashTable *ht_create(size_t num_buckets, size_t key_size, size_t value_size,
                     HashFunc hash, KeyEqFunc key_eq,
                     DestroyFunc key_destroy, DestroyFunc value_destroy);
void ht_destroy(HashTable *table);
int ht_insert(HashTable *table, const void *key, const void *value);
int ht_search(const HashTable *table, const void *key, void *out_value);
int ht_remove(HashTable *table, const void *key);
size_t ht_size(const HashTable *table);
bool ht_is_empty(const HashTable *table);