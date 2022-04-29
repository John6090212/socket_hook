#ifndef _SHARE_H_
#define _SHARE_H_

#include <pthread.h>
#include "share_queue.h"
#include "queue.h"

#define STREAM_QUEUE_CAPACITY 30080
#define DATAGRAM_QUEUE_CAPACITY 20
#define CONNECT_QUEUE_CAPACITY 20
#define ACCEPT_QUEUE_CAPACITY 10
// share memory size for share unit
#define COMMUNICATE_SHM_SIZE 0x400000
// share memory size for connect and accept 
#define CONNECT_SHM_SIZE 0x5000
// share memory size for close
#define CLOSE_SHM_SIZE 0x4000
// share queue number in share memory, an upper bound of max connections at a time of server
#define SOCKET_NUM 10

// share memory for socket communication 
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
int close_shm_fd;
void *close_shm_ptr;
close_unit *close_arr;

#endif