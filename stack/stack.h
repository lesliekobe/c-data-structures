#pragma once

#include <stdlib.h>
#include <string.h>

typedef struct {
    void *data;
    size_t data_size;
    size_t capacity;
    size_t top;
} Stack;

Stack *stack_create(size_t data_size, size_t initial_capacity);
void stack_destroy(Stack *s);
int stack_push(Stack *s, const void *data);
int stack_pop(Stack *s, void *out_data);
int stack_peek(const Stack *s, void *out_data);
size_t stack_size(const Stack *s);
int stack_is_empty(const Stack *s);