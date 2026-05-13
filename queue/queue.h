#pragma once

#include <stdlib.h>
#include <string.h>

typedef struct {
    void *data;
    size_t data_size;
    size_t capacity;
    size_t front;
    size_t rear;
    size_t length;
} Queue;

Queue *queue_create(size_t data_size, size_t initial_capacity);
void queue_destroy(Queue *q);
int queue_enqueue(Queue *q, const void *data);
int queue_dequeue(Queue *q, void *out_data);
int queue_peek(const Queue *q, void *out_data);
size_t queue_size(const Queue *q);
int queue_is_empty(const Queue *q);