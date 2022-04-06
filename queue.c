#include <string.h> //memcpy
#include <stdio.h>
#include <stdlib.h>
#include "queue.h"

// print char array in hex
void print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
}

int enqueue(void *shm, queue *q, char *c, int len){
    char *m_arr = (char *)(shm+q->message_start_offset);

    // queue is full
    if(q->current_size == q->capacity){
        // printf("Queue is full!\n");
        return -1;
    }
    
    int add_len = min((q->capacity-q->current_size), len);
    // queue is empty
    if(q->front == -1){
        q->front = q->rear = 0;
        memcpy(&m_arr[q->rear], c, add_len*sizeof(char));
        q->rear = (add_len - 1) % q->capacity;
        q->current_size += add_len;
    }
    // normal condition
    else{
        q->rear = (q->rear + 1) % q->capacity;
        memcpy(&m_arr[q->rear], c, add_len*sizeof(char));
        q->rear = (q->rear + add_len - 1) % q->capacity;
        q->current_size += add_len;
    }

    return add_len;
}

// need to free buffer and char*
buffer *dequeue(void *shm, queue *q, int len){
    char *m_arr = (char *)(shm+q->message_start_offset);

    int sub_len = min((q->current_size), len);
    char *m = (char *)malloc(sub_len*sizeof(char));
    buffer *b = (buffer *)malloc(sizeof(buffer));
    if(q->front == -1){
        printf("Queue is empty!\n");
        free(m);
        m = NULL;
        b->buf = m;
        b->length = 0;
        return b;
    }
    
    memcpy(m, &m_arr[q->front], sub_len*sizeof(char));
    memset(&m_arr[q->front], 0, sub_len*sizeof(char));
    q->current_size -= sub_len;
    // reset front and rear if queue becomes empty
    if(q->current_size == 0){
        q->front = -1;
        q->rear = -1;
    }
    // normal condition
    else
        q->front = (q->front + sub_len) % q->capacity;
    
    b->buf = m;
    b->length = sub_len;

    return b;
}

// need to fix later
void display_queue(void *shm){
    queue *q = (queue *)shm;
    char *m_arr = (char *)(shm+q->message_start_offset);
    if (q->front == -1){
        printf("Queue is empty!\n");
        return;
    }
    printf("Message in queue are: \n");
    if (q->rear >= q->front){
        for(int i = q->front; i <= q->rear; i++){
            char *m = ((char *)&m_arr[i]) + sizeof(int);
            print_hex(m, 1);
        }
    }
    else{
        for(int i = q->front; i < q->capacity; i++){
            char *m = ((char *)&m_arr[i]) + sizeof(int);
            print_hex(m, 1);
        }
        for(int i = 0; i <= q->rear; i++){
            char *m = ((char *)&m_arr[i]) + sizeof(int);
            print_hex(m, 1);
        }
    }
}