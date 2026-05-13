#include "binary_tree.h"
#include <stdlib.h>
#include <string.h>

TreeNode *create_node(BinaryTree *tree, const void *data) {
    TreeNode *node = (TreeNode *)malloc(sizeof(TreeNode));
    if (!node) return NULL;
    node->data = malloc(tree->data_size);
    if (!node->data) {
        free(node);
        return NULL;
    }
    memcpy(node->data, data, tree->data_size);
    node->left = NULL;
    node->right = NULL;
    return node;
}

BinaryTree *btree_create(size_t data_size, CompareFunc compare, DestroyFunc destroy) {
    BinaryTree *tree = (BinaryTree *)malloc(sizeof(BinaryTree));
    if (!tree) return NULL;
    tree->root = NULL;
    tree->data_size = data_size;
    tree->compare = compare;
    tree->destroy = destroy;
    return tree;
}

void btree_destroy(BinaryTree *tree) {
    if (!tree) return;
    if (tree->destroy && tree->root) {
        void destroy_node(TreeNode *n) {
            if (!n) return;
            destroy_node(n->left);
            destroy_node(n->right);
            tree->destroy(n->data);
            free(n->data);
            free(n);
        }
        destroy_node(tree->root);
    } else {
        void free_node(TreeNode *n) {
            if (!n) return;
            free_node(n->left);
            free_node(n->right);
            free(n->data);
            free(n);
        }
        free_node(tree->root);
    }
    free(tree);
}

static int insert_node(TreeNode **node, BinaryTree *tree, const void *data) {
    if (!*node) {
        *node = create_node(tree, data);
        return (*node) ? 0 : -1;
    }
    int cmp = tree->compare(data, (*node)->data);
    if (cmp < 0) {
        return insert_node(&(*node)->left, tree, data);
    } else if (cmp > 0) {
        return insert_node(&(*node)->right, tree, data);
    }
    return 0;
}

int btree_insert(BinaryTree *tree, const void *data) {
    if (!tree || !data) return -1;
    return insert_node(&tree->root, tree, data);
}

static int search_node(TreeNode *node, BinaryTree *tree, const void *key, void *out_data) {
    if (!node) return -1;
    int cmp = tree->compare(key, node->data);
    if (cmp == 0) {
        if (out_data) memcpy(out_data, node->data, tree->data_size);
        return 0;
    } else if (cmp < 0) {
        return search_node(node->left, tree, key, out_data);
    } else {
        return search_node(node->right, tree, key, out_data);
    }
}

int btree_search(const BinaryTree *tree, const void *key, void *out_data) {
    if (!tree) return -1;
    return search_node(tree->root, (BinaryTree *)tree, key, out_data);
}

static TreeNode *find_min(TreeNode *node) {
    while (node && node->left) node = node->left;
    return node;
}

static int remove_node(TreeNode **node, BinaryTree *tree, const void *key) {
    if (!*node) return -1;
    int cmp = tree->compare(key, (*node)->data);
    if (cmp < 0) {
        return remove_node(&(*node)->left, tree, key);
    } else if (cmp > 0) {
        return remove_node(&(*node)->right, tree, key);
    } else {
        TreeNode *to_remove = *node;
        if (!(*node)->left && !(*node)->right) {
            *node = NULL;
        } else if (!(*node)->left) {
            *node = (*node)->right;
        } else if (!(*node)->right) {
            *node = (*node)->left;
        } else {
            TreeNode *successor = find_min((*node)->right);
            memcpy(to_remove->data, successor->data, tree->data_size);
            return remove_node(&(*node)->right, tree, successor->data);
        }
        free(to_remove->data);
        free(to_remove);
        return 0;
    }
}

int btree_remove(BinaryTree *tree, const void *key) {
    if (!tree) return -1;
    return remove_node(&tree->root, tree, key);
}

static void inorder(TreeNode *node, VisitFunc visit) {
    if (!node) return;
    inorder(node->left, visit);
    visit(node->data);
    inorder(node->right, visit);
}

static void preorder(TreeNode *node, VisitFunc visit) {
    if (!node) return;
    visit(node->data);
    preorder(node->left, visit);
    preorder(node->right, visit);
}

static void postorder(TreeNode *node, VisitFunc visit) {
    if (!node) return;
    postorder(node->left, visit);
    postorder(node->right, visit);
    visit(node->data);
}

void btree_inorder(const BinaryTree *tree, VisitFunc visit) {
    if (!tree) return;
    inorder(tree->root, visit);
}

void btree_preorder(const BinaryTree *tree, VisitFunc visit) {
    if (!tree) return;
    preorder(tree->root, visit);
}

void btree_postorder(const BinaryTree *tree, VisitFunc visit) {
    if (!tree) return;
    postorder(tree->root, visit);
}

static size_t size_node(TreeNode *node) {
    if (!node) return 0;
    return 1 + size_node(node->left) + size_node(node->right);
}

size_t btree_size(const BinaryTree *tree) {
    return tree ? size_node(tree->root) : 0;
}

bool btree_is_empty(const BinaryTree *tree) {
    return tree ? (tree->root == NULL) : true;
}