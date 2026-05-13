#include "doubly_linked_list.h"
#include <stdlib.h>
#include <string.h>

DoublyLinkedList *dll_create(size_t data_size) {
    DoublyLinkedList *list = (DoublyLinkedList *)malloc(sizeof(DoublyLinkedList));
    if (!list) return NULL;
    list->head = NULL;
    list->tail = NULL;
    list->data_size = data_size;
    list->length = 0;
    return list;
}

void dll_destroy(DoublyLinkedList *list) {
    if (!list) return;
    dll_clear(list);
    free(list);
}

static DListNode *create_node(DoublyLinkedList *list, const void *data) {
    DListNode *node = (DListNode *)malloc(sizeof(DListNode));
    if (!node) return NULL;
    node->data = malloc(list->data_size);
    if (!node->data) {
        free(node);
        return NULL;
    }
    memcpy(node->data, data, list->data_size);
    node->next = NULL;
    node->prev = NULL;
    return node;
}

int dll_append(DoublyLinkedList *list, const void *data) {
    return dll_insert_at(list, list->length, data);
}

int dll_prepend(DoublyLinkedList *list, const void *data) {
    return dll_insert_at(list, 0, data);
}

int dll_insert_at(DoublyLinkedList *list, size_t index, const void *data) {
    if (index > list->length) return -1;

    DListNode *new_node = create_node(list, data);
    if (!new_node) return -1;

    if (list->length == 0) {
        list->head = new_node;
        list->tail = new_node;
    } else if (index == 0) {
        new_node->next = list->head;
        list->head->prev = new_node;
        list->head = new_node;
    } else if (index == list->length) {
        new_node->prev = list->tail;
        list->tail->next = new_node;
        list->tail = new_node;
    } else {
        DListNode *cur = list->head;
        for (size_t i = 0; i < index; i++) {
            cur = cur->next;
        }
        new_node->next = cur;
        new_node->prev = cur->prev;
        cur->prev->next = new_node;
        cur->prev = new_node;
    }

    list->length++;
    return 0;
}

int dll_remove_at(DoublyLinkedList *list, size_t index, void *out_data) {
    if (index >= list->length) return -1;

    DListNode *to_remove;
    if (index == 0) {
        to_remove = list->head;
        list->head = to_remove->next;
        if (list->head) list->head->prev = NULL;
        else list->tail = NULL;
    } else if (index == list->length - 1) {
        to_remove = list->tail;
        list->tail = to_remove->prev;
        if (list->tail) list->tail->next = NULL;
        else list->head = NULL;
    } else {
        to_remove = list->head;
        for (size_t i = 0; i < index; i++) {
            to_remove = to_remove->next;
        }
        to_remove->prev->next = to_remove->next;
        to_remove->next->prev = to_remove->prev;
    }

    if (out_data) {
        memcpy(out_data, to_remove->data, list->data_size);
    }
    free(to_remove->data);
    free(to_remove);
    list->length--;
    return 0;
}

int dll_get_at(DoublyLinkedList *list, size_t index, void *out_data) {
    if (index >= list->length) return -1;

    DListNode *node;
    if (index < list->length / 2) {
        node = list->head;
        for (size_t i = 0; i < index; i++) {
            node = node->next;
        }
    } else {
        node = list->tail;
        for (size_t i = list->length - 1; i > index; i--) {
            node = node->prev;
        }
    }
    memcpy(out_data, node->data, list->data_size);
    return 0;
}

size_t dll_length(DoublyLinkedList *list) {
    return list ? list->length : 0;
}

int dll_clear(DoublyLinkedList *list) {
    if (!list) return -1;
    DListNode *current = list->head;
    while (current) {
        DListNode *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    list->head = NULL;
    list->tail = NULL;
    list->length = 0;
    return 0;
}