#pragma once

#include <stdlib.h>
#include <string.h>

typedef struct DListNode {
    void *data;
    struct DListNode *next;
    struct DListNode *prev;
} DListNode;

typedef struct {
    DListNode *head;
    DListNode *tail;
    size_t data_size;
    size_t length;
} DoublyLinkedList;

DoublyLinkedList *dll_create(size_t data_size);
void dll_destroy(DoublyLinkedList *list);
int dll_append(DoublyLinkedList *list, const void *data);
int dll_prepend(DoublyLinkedList *list, const void *data);
int dll_insert_at(DoublyLinkedList *list, size_t index, const void *data);
int dll_remove_at(DoublyLinkedList *list, size_t index, void *out_data);
int dll_get_at(DoublyLinkedList *list, size_t index, void *out_data);
size_t dll_length(DoublyLinkedList *list);
int dll_clear(DoublyLinkedList *list);