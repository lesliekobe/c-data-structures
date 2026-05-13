#include "stack.h"
#include <stdlib.h>
#include <string.h>

Stack *stack_create(size_t data_size, size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 16;
    Stack *s = (Stack *)malloc(sizeof(Stack));
    if (!s) return NULL;

    s->data = malloc(data_size * initial_capacity);
    if (!s->data) {
        free(s);
        return NULL;
    }

    s->data_size = data_size;
    s->capacity = initial_capacity;
    s->top = 0;
    return s;
}

void stack_destroy(Stack *s) {
    if (!s) return;
    free(s->data);
    free(s);
}

static int grow(Stack *s) {
    size_t new_cap = s->capacity * 2;
    void *new_data = realloc(s->data, s->data_size * new_cap);
    if (!new_data) return -1;
    s->data = new_data;
    s->capacity = new_cap;
    return 0;
}

int stack_push(Stack *s, const void *data) {
    if (!s || !data) return -1;
    if (s->top >= s->capacity) {
        if (grow(s) != 0) return -1;
    }
    memcpy((char *)s->data + s->data_size * s->top, data, s->data_size);
    s->top++;
    return 0;
}

int stack_pop(Stack *s, void *out_data) {
    if (!s || stack_is_empty(s)) return -1;
    s->top--;
    if (out_data) {
        memcpy(out_data, (char *)s->data + s->data_size * s->top, s->data_size);
    }
    return 0;
}

int stack_peek(const Stack *s, void *out_data) {
    if (!s || stack_is_empty(s)) return -1;
    if (out_data) {
        memcpy(out_data, (char *)s->data + s->data_size * (s->top - 1), s->data_size);
    }
    return 0;
}

size_t stack_size(const Stack *s) {
    return s ? s->top : 0;
}

int stack_is_empty(const Stack *s) {
    return s ? (s->top == 0) : 1;
}