#include "queue.h"
#include <stdlib.h>
#include <string.h>

Queue *queue_create(size_t data_size, size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 16;
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (!q) return NULL;

    q->data = malloc(data_size * initial_capacity);
    if (!q->data) {
        free(q);
        return NULL;
    }

    q->data_size = data_size;
    q->capacity = initial_capacity;
    q->front = 0;
    q->rear = 0;
    q->length = 0;
    return q;
}

void queue_destroy(Queue *q) {
    if (!q) return;
    free(q->data);
    free(q);
}

static int grow(Queue *q) {
    size_t new_cap = q->capacity * 2;
    void *new_data = malloc(q->data_size * new_cap);
    if (!new_data) return -1;

    if (q->front <= q->rear) {
        memcpy(new_data, (char *)q->data + q->front * q->data_size,
               q->length * q->data_size);
    } else {
        size_t first_part = (q->capacity - q->front) * q->data_size;
        memcpy(new_data, (char *)q->data + q->front * q->data_size, first_part);
        memcpy((char *)new_data + first_part, q->data, q->rear * q->data_size);
    }

    free(q->data);
    q->data = new_data;
    q->front = 0;
    q->rear = q->length;
    q->capacity = new_cap;
    return 0;
}

int queue_enqueue(Queue *q, const void *data) {
    if (!q || !data) return -1;
    if (q->length >= q->capacity) {
        if (grow(q) != 0) return -1;
    }
    memcpy((char *)q->data + q->rear * q->data_size, data, q->data_size);
    q->rear = (q->rear + 1) % q->capacity;
    q->length++;
    return 0;
}

int queue_dequeue(Queue *q, void *out_data) {
    if (!q || queue_is_empty(q)) return -1;
    if (out_data) {
        memcpy(out_data, (char *)q->data + q->front * q->data_size, q->data_size);
    }
    q->front = (q->front + 1) % q->capacity;
    q->length--;
    return 0;
}

int queue_peek(const Queue *q, void *out_data) {
    if (!q || queue_is_empty(q)) return -1;
    if (out_data) {
        memcpy(out_data, (char *)q->data + q->front * q->data_size, q->data_size);
    }
    return 0;
}

size_t queue_size(const Queue *q) {
    return q ? q->length : 0;
}

int queue_is_empty(const Queue *q) {
    return q ? (q->length == 0) : 1;
}