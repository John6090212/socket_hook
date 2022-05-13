#ifndef _QUEUE_H_
#define _QUEUE_H_

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <string.h>

typedef struct Queue Queue;

// A structure to represent a queue
struct Queue {
    int front, rear, size;
    unsigned capacity;
    int* array;
};

typedef struct int_array int_array;
struct int_array{
    int *arr;
    int length;
};

Queue* createQueue(unsigned capacity);
int isFullQueue(Queue* queue);
int isEmptyQueue(Queue* queue);
int enqueue(Queue* queue, int item);
int dequeue(Queue* queue);
void destroyQueue(Queue* queue);
int_array *getQueueArray(Queue* queue);

// connect queue
typedef struct connection connection;
struct connection{
    struct sockaddr addr;
    int client_fd; // for accept queue checking
};

typedef struct connect_queue connect_queue;

struct connect_queue {
    int front, rear, size;
    unsigned capacity;
    int queue_start_offset;
};

int isFullConnectQueue(connect_queue* queue);
int isEmptyConnectQueue(connect_queue* queue);
int Connect_enqueue(void *shm, connect_queue* queue, connection item);
connection* Connect_dequeue(void *shm, connect_queue* queue);

// accept queue
typedef struct acception acception;
struct acception{
    int client_fd;
    int share_unit_index;
    int response_su_index;
};

typedef struct accept_queue accept_queue;

struct accept_queue {
    int front, rear, size;
    unsigned capacity;
    int queue_start_offset;
};

int isFullAcceptQueue(accept_queue* queue);
int isEmptyAcceptQueue(accept_queue* queue);
int Accept_enqueue(void *shm, accept_queue* queue, acception item);
acception* Accept_dequeue(void *shm, accept_queue* queue);

#endif