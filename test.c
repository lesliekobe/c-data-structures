#include <stdio.h>
#include <assert.h>
#include "linked_list/linked_list.h"
#include "doubly_linked_list/doubly_linked_list.h"
#include "stack/stack.h"
#include "queue/queue.h"
#include "binary_tree/binary_tree.h"
#include "hash_table/hash_table.h"

static int int_compare(const void *a, const void *b) {
    return (*(int *)a) - (*(int *)b);
}

static size_t int_hash(const void *key) {
    return (size_t)(*(int *)key);
}

static void print_int(const void *data) {
    printf("%d ", *(int *)data);
}

int main(void) {
    printf("=== Linked List Test ===\n");
    LinkedList *ll = ll_create(sizeof(int));
    int vals1[] = {10, 20, 30};
    for (int i = 0; i < 3; i++) ll_append(ll, &vals1[i]);
    for (size_t i = 0; i < ll_length(ll); i++) {
        int v; ll_get_at(ll, i, &v);
        printf("%d ", v);
    }
    printf("\n");
    ll_destroy(ll);

    printf("\n=== Doubly Linked List Test ===\n");
    DoublyLinkedList *dll = dll_create(sizeof(int));
    for (int i = 0; i < 3; i++) dll_append(dll, &vals1[i]);
    for (size_t i = 0; i < dll_length(dll); i++) {
        int v; dll_get_at(dll, i, &v);
        printf("%d ", v);
    }
    printf("\n");
    dll_destroy(dll);

    printf("\n=== Stack Test ===\n");
    Stack *s = stack_create(sizeof(int), 0);
    for (int i = 0; i < 3; i++) stack_push(s, &vals1[i]);
    while (!stack_is_empty(s)) {
        int v; stack_pop(s, &v);
        printf("%d ", v);
    }
    printf("\n");
    stack_destroy(s);

    printf("\n=== Queue Test ===\n");
    Queue *q = queue_create(sizeof(int), 0);
    for (int i = 0; i < 3; i++) queue_enqueue(q, &vals1[i]);
    while (!queue_is_empty(q)) {
        int v; queue_dequeue(q, &v);
        printf("%d ", v);
    }
    printf("\n");
    queue_destroy(q);

    printf("\n=== Binary Tree Test ===\n");
    BinaryTree *bt = btree_create(sizeof(int), int_compare, NULL);
    int vals2[] = {50, 30, 70, 20, 40, 60, 80};
    for (int i = 0; i < 7; i++) btree_insert(bt, &vals2[i]);
    printf("Inorder: "); btree_inorder(bt, print_int); printf("\n");
    int key = 40, result;
    if (btree_search(bt, &key, &result) == 0) {
        printf("Found: %d\n", result);
    }
    btree_destroy(bt);

    printf("\n=== Hash Table Test ===\n");
    HashTable *ht = ht_create(16, sizeof(int), sizeof(int), int_hash, int_compare, NULL, NULL);
    for (int i = 0; i < 5; i++) {
        int k = i * 10, v = i * 100;
        ht_insert(ht, &k, &v);
    }
    key = 30;
    if (ht_search(ht, &key, &result) == 0) {
        printf("Found: %d\n", result);
    }
    ht_destroy(ht);

    printf("\nAll tests passed!\n");
    return 0;
}