#pragma once

#include <stdlib.h>
#include <string.h>

typedef struct ListNode {
    void *data;
    struct ListNode *next;
} ListNode;

typedef struct {
    ListNode *head;
    size_t data_size;
    size_t length;
} LinkedList;

LinkedList *ll_create(size_t data_size);
void ll_destroy(LinkedList *list);
int ll_append(LinkedList *list, const void *data);
int ll_prepend(LinkedList *list, const void *data);
int ll_insert_at(LinkedList *list, size_t index, const void *data);
int ll_remove_at(LinkedList *list, size_t index, void *out_data);
int ll_get_at(LinkedList *list, size_t index, void *out_data);
size_t ll_length(LinkedList *list);
int ll_clear(LinkedList *list);