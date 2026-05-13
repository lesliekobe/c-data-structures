#include "linked_list.h"
#include <stdlib.h>
#include <string.h>

LinkedList *ll_create(size_t data_size) {
    LinkedList *list = (LinkedList *)malloc(sizeof(LinkedList));
    if (!list) return NULL;
    list->head = NULL;
    list->data_size = data_size;
    list->length = 0;
    return list;
}

void ll_destroy(LinkedList *list) {
    if (!list) return;
    ll_clear(list);
    free(list);
}

int ll_append(LinkedList *list, const void *data) {
    return ll_insert_at(list, list->length, data);
}

int ll_prepend(LinkedList *list, const void *data) {
    return ll_insert_at(list, 0, data);
}

int ll_insert_at(LinkedList *list, size_t index, const void *data) {
    if (index > list->length) return -1;

    ListNode *new_node = (ListNode *)malloc(sizeof(ListNode));
    if (!new_node) return -1;

    new_node->data = malloc(list->data_size);
    if (!new_node->data) {
        free(new_node);
        return -1;
    }
    memcpy(new_node->data, data, list->data_size);

    if (index == 0) {
        new_node->next = list->head;
        list->head = new_node;
    } else {
        ListNode *prev = list->head;
        for (size_t i = 0; i < index - 1; i++) {
            prev = prev->next;
        }
        new_node->next = prev->next;
        prev->next = new_node;
    }

    list->length++;
    return 0;
}

int ll_remove_at(LinkedList *list, size_t index, void *out_data) {
    if (index >= list->length) return -1;

    ListNode *to_remove;
    if (index == 0) {
        to_remove = list->head;
        list->head = to_remove->next;
    } else {
        ListNode *prev = list->head;
        for (size_t i = 0; i < index - 1; i++) {
            prev = prev->next;
        }
        to_remove = prev->next;
        prev->next = to_remove->next;
    }

    if (out_data) {
        memcpy(out_data, to_remove->data, list->data_size);
    }
    free(to_remove->data);
    free(to_remove);
    list->length--;
    return 0;
}

int ll_get_at(LinkedList *list, size_t index, void *out_data) {
    if (index >= list->length) return -1;

    ListNode *node = list->head;
    for (size_t i = 0; i < index; i++) {
        node = node->next;
    }
    memcpy(out_data, node->data, list->data_size);
    return 0;
}

size_t ll_length(LinkedList *list) {
    return list ? list->length : 0;
}

int ll_clear(LinkedList *list) {
    if (!list) return -1;
    ListNode *current = list->head;
    while (current) {
        ListNode *next = current->next;
        free(current->data);
        free(current);
        current = next;
    }
    list->head = NULL;
    list->length = 0;
    return 0;
}