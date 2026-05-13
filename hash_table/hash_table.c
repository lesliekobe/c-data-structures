#include "hash_table.h"
#include <stdlib.h>
#include <string.h>

HashTable *ht_create(size_t num_buckets, size_t key_size, size_t value_size,
                     HashFunc hash, KeyEqFunc key_eq,
                     DestroyFunc key_destroy, DestroyFunc value_destroy) {
    if (num_buckets == 0) num_buckets = 16;
    HashTable *table = (HashTable *)malloc(sizeof(HashTable));
    if (!table) return NULL;

    table->buckets = (HashEntry **)calloc(num_buckets, sizeof(HashEntry *));
    if (!table->buckets) {
        free(table);
        return NULL;
    }

    table->num_buckets = num_buckets;
    table->key_size = key_size;
    table->value_size = value_size;
    table->size = 0;
    table->hash = hash;
    table->key_eq = key_eq;
    table->key_destroy = key_destroy;
    table->value_destroy = value_destroy;
    return table;
}

void ht_destroy(HashTable *table) {
    if (!table) return;
    for (size_t i = 0; i < table->num_buckets; i++) {
        HashEntry *entry = table->buckets[i];
        while (entry) {
            HashEntry *next = entry->next;
            if (table->key_destroy) table->key_destroy(entry->key);
            else free(entry->key);
            if (table->value_destroy) table->value_destroy(entry->value);
            else free(entry->value);
            free(entry);
            entry = next;
        }
    }
    free(table->buckets);
    free(table);
}

int ht_insert(HashTable *table, const void *key, const void *value) {
    if (!table || !key || !value) return -1;

    size_t idx = table->hash(key) % table->num_buckets;
    HashEntry *entry = table->buckets[idx];
    while (entry) {
        if (table->key_eq(entry->key, key) == 0) {
            if (table->value_destroy) table->value_destroy(entry->value);
            else free(entry->value);
            entry->value = malloc(table->value_size);
            if (!entry->value) return -1;
            memcpy(entry->value, value, table->value_size);
            return 0;
        }
        entry = entry->next;
    }

    HashEntry *new_entry = (HashEntry *)malloc(sizeof(HashEntry));
    if (!new_entry) return -1;
    new_entry->key = malloc(table->key_size);
    new_entry->value = malloc(table->value_size);
    if (!new_entry->key || !new_entry->value) {
        free(new_entry->key);
        free(new_entry->value);
        free(new_entry);
        return -1;
    }
    memcpy(new_entry->key, key, table->key_size);
    memcpy(new_entry->value, value, table->value_size);
    new_entry->next = table->buckets[idx];
    table->buckets[idx] = new_entry;
    table->size++;
    return 0;
}

int ht_search(const HashTable *table, const void *key, void *out_value) {
    if (!table || !key) return -1;

    size_t idx = table->hash(key) % table->num_buckets;
    HashEntry *entry = table->buckets[idx];
    while (entry) {
        if (table->key_eq(entry->key, key) == 0) {
            if (out_value) memcpy(out_value, entry->value, table->value_size);
            return 0;
        }
        entry = entry->next;
    }
    return -1;
}

int ht_remove(HashTable *table, const void *key) {
    if (!table || !key) return -1;

    size_t idx = table->hash(key) % table->num_buckets;
    HashEntry *entry = table->buckets[idx];
    HashEntry *prev = NULL;

    while (entry) {
        if (table->key_eq(entry->key, key) == 0) {
            if (prev) prev->next = entry->next;
            else table->buckets[idx] = entry->next;
            if (table->key_destroy) table->key_destroy(entry->key);
            else free(entry->key);
            if (table->value_destroy) table->value_destroy(entry->value);
            else free(entry->value);
            free(entry);
            table->size--;
            return 0;
        }
        prev = entry;
        entry = entry->next;
    }
    return -1;
}

size_t ht_size(const HashTable *table) {
    return table ? table->size : 0;
}

bool ht_is_empty(const HashTable *table) {
    return table ? (table->size == 0) : true;
}