#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct Queue Queue;

// A structure to represent a queue
struct Queue {
    int front, rear, size;
    unsigned capacity;
    int* array;
};

struct Queue* createQueue(unsigned capacity);
int isFull(struct Queue* queue);
int isEmpty(struct Queue* queue);
int enqueue(struct Queue* queue, int item);
int dequeue(struct Queue* queue);

#endif