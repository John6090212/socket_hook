// https://www.geeksforgeeks.org/queue-set-1introduction-and-array-implementation/
// C program for array implementation of queue
#include "queue.h"
 
// function to create a queue
// of given capacity.
// It initializes size of queue as 0
Queue* createQueue(unsigned capacity)
{
    Queue* queue = (Queue*)malloc(
        sizeof(Queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
 
    // This is important, see the enqueue
    queue->rear = capacity - 1;
    queue->array = (int*)malloc(
        queue->capacity * sizeof(int));
    return queue;
}
 
// Queue is full when size becomes
// equal to the capacity
int isFullQueue(Queue* queue)
{
    return (queue->size == queue->capacity);
}
 
// Queue is empty when size is 0
int isEmptyQueue(Queue* queue)
{
    return (queue->size == 0);
}
 
// Function to add an item to the queue.
// It changes rear and size
int enqueue(Queue* queue, int item)
{
    if (isFullQueue(queue))
        return -1;
    queue->rear = (queue->rear + 1)
                  % queue->capacity;
    queue->array[queue->rear] = item;
    queue->size = queue->size + 1;
    return 0;
}
 
// Function to remove an item from queue.
// It changes front and size
int dequeue(Queue* queue)
{
    if (isEmptyQueue(queue))
        return INT_MIN;
    int item = queue->array[queue->front];
    queue->front = (queue->front + 1)
                   % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

void destroyQueue(Queue* queue){
    if(queue != NULL){
        if(queue->array != NULL)
            free(queue->array);
        free(queue);
    }
}

int_array *getQueueArray(Queue* queue){
    if(queue == NULL || queue->size == 0)
        return NULL;

    int *arr = (int *)malloc(queue->size*sizeof(int));
    for(int i = 0; i < queue->size; i++){
        arr[i] = queue->array[(queue->front+i)%queue->capacity];
    }
    int_array *ia = (int_array *)malloc(sizeof(int_array));
    ia->arr = arr;
    ia->length = queue->size;

    return ia;
}

// connect queue function
connect_queue* createConnectQueue(unsigned capacity){
    connect_queue* queue = (connect_queue*)malloc(
        sizeof(struct connect_queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
 
    // This is important, see the enqueue
    queue->rear = capacity - 1;
    return queue;
}

int isFullConnectQueue(connect_queue* queue){
    return (queue->size == queue->capacity);
}

int isEmptyConnectQueue(connect_queue* queue){
    return (queue->size == 0);
}

int Connect_enqueue(void *shm, connect_queue* queue, connection item){
    connection *array = (connection *)(shm+queue->queue_start_offset);
    if (isFullConnectQueue(queue))
        return -1;
    queue->rear = (queue->rear + 1)
                  % queue->capacity;
    memcpy(&array[queue->rear], &item, sizeof(connection));
    queue->size = queue->size + 1;
    return 0;
}

connection* Connect_dequeue(void *shm, connect_queue* queue){
    connection *array = (connection *)(shm+queue->queue_start_offset);
    if (isEmptyConnectQueue(queue))
        return NULL;
    connection *item = (connection*)malloc(sizeof(connection));
    memcpy(item, &array[queue->front], sizeof(connection));
    queue->front = (queue->front + 1)
                   % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}

// accept queue function
accept_queue* createAcceptQueue(unsigned capacity){
    accept_queue* queue = (accept_queue*)malloc(
        sizeof(struct accept_queue));
    queue->capacity = capacity;
    queue->front = queue->size = 0;
 
    // This is important, see the enqueue
    queue->rear = capacity - 1;

    return queue;
}

int isFullAcceptQueue(accept_queue* queue){
    return (queue->size == queue->capacity);
}

int isEmptyAcceptQueue(accept_queue* queue){
    return (queue->size == 0);
}

int Accept_enqueue(void *shm, accept_queue* queue, acception item){
    acception *array = (acception *)(shm+queue->queue_start_offset);
    if (isFullAcceptQueue(queue))
        return -1;
    queue->rear = (queue->rear + 1)
                  % queue->capacity;
    memcpy(&array[queue->rear], &item, sizeof(acception));
    queue->size = queue->size + 1;
    return 0;
}

acception* Accept_dequeue(void *shm, accept_queue* queue){
    acception *array = (acception *)(shm+queue->queue_start_offset);
    if (isEmptyAcceptQueue(queue))
        return NULL;
    acception *item = (acception*)malloc(sizeof(acception));
    memcpy(item, &array[queue->front], sizeof(acception));
    queue->front = (queue->front + 1)
                   % queue->capacity;
    queue->size = queue->size - 1;
    return item;
}