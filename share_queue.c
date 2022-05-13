#include <string.h> //memcpy
#include <stdio.h>
#include <stdlib.h>
#include "share_queue.h"

// print char array in hex
void print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
}

int stream_enqueue(void *shm, share_queue *q, char *c, int len){
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
    }
    // normal condition
    else{
        q->rear = (q->rear + 1) % q->capacity;
        memcpy(&m_arr[q->rear], c, add_len*sizeof(char));
        q->rear = (q->rear + add_len - 1) % q->capacity;
        
    }
    q->current_size += add_len;

    return add_len;
}

// need to free buffer and char*
buffer *stream_dequeue(void *shm, share_queue *q, int len){
    char *m_arr = (char *)(shm+q->message_start_offset);

    int sub_len = min((q->current_size), len);
    char *m = (char *)malloc(sub_len*sizeof(char));
    buffer *b = (buffer *)malloc(sizeof(buffer));
    if(q->front == -1){
        // printf("Queue is empty!\n");
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

int datagram_enqueue(void *shm, share_queue *q, message_t m){
    message_t *m_arr = (message_t *)(shm+q->message_start_offset);
    // queue is full
    if (q->current_size == q->capacity){
        return -1;
    }
    // queue is empty
    if(q->front == -1){
        q->front = q->rear = 0;
        memcpy(&m_arr[q->rear], &m, sizeof(message_t));
    }
    // normal condition
    else{
        q->rear = (q->rear + 1) % q->capacity;
        memcpy(&m_arr[q->rear], &m, sizeof(message_t));
    }
    q->current_size++;

    return 0;
}

message_t *datagram_dequeue(void *shm, share_queue *q){
    message_t *m_arr = (message_t *)(shm+q->message_start_offset);
    message_t *m = (message_t *)malloc(sizeof(message_t));
    if(q->front == -1){
        m->length = 0;
        return m;
    }
    
    memcpy(m, &m_arr[q->front], sizeof(message_t));
    m_arr[q->front].length = 0;
    // reset front and rear if queue becomes empty
    if(q->front == q->rear){
        q->front = -1;
        q->rear = -1;
    }
    // normal condition
    else
        q->front = (q->front + 1) % q->capacity;
    
    q->current_size--;

    return m;    
}

int int_enqueue(void *shm, share_queue *q, int m){
    int *m_arr = (int *)(shm+q->message_start_offset);
    // queue is full
    if (q->current_size == q->capacity){
        return -1;
    }
    // queue is empty
    if(q->front == -1){
        q->front = q->rear = 0;
        m_arr[q->rear] = m;
    }
    // normal condition
    else{
        q->rear = (q->rear + 1) % q->capacity;
        m_arr[q->rear] = m;
    }
    q->current_size++;

    return 0;
}

int int_dequeue(void *shm, share_queue *q){
    int *m_arr = (int *)(shm+q->message_start_offset);
    int m;
    if(q->front == -1)
        return -1;
    
    m = m_arr[q->front];
    // reset front and rear if queue becomes empty
    if(q->front == q->rear){
        q->front = -1;
        q->rear = -1;
    }
    // normal condition
    else
        q->front = (q->front + 1) % q->capacity;
    
    q->current_size--;

    return m;    
}

// need to fix later
void display_queue(void *shm){
    share_queue *q = (share_queue *)shm;
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