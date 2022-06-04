#ifndef _SHARE_H_
#define _SHARE_H_

#include <pthread.h>
#include "share_queue.h"
#include "queue.h"

#define DATAGRAM_QUEUE_CAPACITY 50
#define STREAM_QUEUE_CAPACITY DATAGRAM_QUEUE_CAPACITY*sizeof(message_t)
#define CONNECT_QUEUE_CAPACITY 20
#define ACCEPT_QUEUE_CAPACITY 10
#define INT_QUEUE_CAPACITY 7520
// share memory size for share unit
#define COMMUNICATE_SHM_SIZE (sizeof(share_unit)*SOCKET_NUM)
// share memory size for connect and accept 
#define CONNECT_SHM_SIZE 0x5000
// share memory size for close
#define CLOSE_SHM_SIZE 0x4000
// share queue number in share memory, an upper bound of max connections at a time of server
#define SOCKET_NUM 5

// share memory for socket communication 
char *shm_name;
int shm_fd;
void *shm_ptr;

typedef struct share_unit share_unit;
struct share_unit {
    share_queue request_queue;
    share_queue response_queue;
    pthread_mutex_t request_lock;
    pthread_mutex_t response_lock;
    char request_buf[STREAM_QUEUE_CAPACITY];
    char response_buf[STREAM_QUEUE_CAPACITY];
};

// share memory for socket connection
char *connect_shm_name;
int connect_shm_fd;
void *connect_shm_ptr;
connect_queue *connect_queue_ptr;
accept_queue *accept_queue_ptr;
pthread_mutex_t *connect_lock;
pthread_mutex_t *accept_lock;

typedef struct close_unit close_unit;
struct close_unit {
    int client_read;
    int client_write;
    int server_read;
    int server_write;
};

// share memory for socket close
char *close_shm_name;
int close_shm_fd;
void *close_shm_ptr;
close_unit *close_arr;

#endif