#pragma once

#include <stdlib.h>
#include <stdbool.h>

typedef struct TreeNode {
    void *data;
    struct TreeNode *left;
    struct TreeNode *right;
} TreeNode;

typedef int (*CompareFunc)(const void *a, const void *b);
typedef void (*VisitFunc)(const void *data);
typedef void (*DestroyFunc)(void *data);

typedef struct {
    TreeNode *root;
    size_t data_size;
    CompareFunc compare;
    DestroyFunc destroy;
} BinaryTree;

BinaryTree *btree_create(size_t data_size, CompareFunc compare, DestroyFunc destroy);
void btree_destroy(BinaryTree *tree);
int btree_insert(BinaryTree *tree, const void *data);
int btree_search(const BinaryTree *tree, const void *key, void *out_data);
int btree_remove(BinaryTree *tree, const void *key);
void btree_inorder(const BinaryTree *tree, VisitFunc visit);
void btree_preorder(const BinaryTree *tree, VisitFunc visit);
void btree_postorder(const BinaryTree *tree, VisitFunc visit);
size_t btree_size(const BinaryTree *tree);
bool btree_is_empty(const BinaryTree *tree);