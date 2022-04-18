#ifndef _SHARE_H_
#define _SHARE_H_

#include <pthread.h>
#include "share_queue.h"

#define STREAM_QUEUE_CAPACITY 30080
#define DATAGRAM_QUEUE_CAPACITY 20
// share memory size for share unit
#define COMMUNICATE_SHM_SIZE 0x400000
// share memory size for checking socket bind arress of share unit 
#define CONNECT_SHM_SIZE 0x5000
// share queue number in share memory, an upper bound of max connections at a time of server
#define SOCKET_NUM 10

// share memory for socket communication 
int shm_fd;
void *shm_ptr;
// share memory for socket connection share memory
int connect_shm_fd;
void *connect_shm_ptr;
pthread_mutex_t *connect_lock;
struct sockaddr *connect_sa_ptr;

typedef struct share_unit share_unit;
struct share_unit {
    share_queue request_queue;
    share_queue response_queue;
    pthread_mutex_t request_lock;
    pthread_mutex_t response_lock;
    char request_buf[STREAM_QUEUE_CAPACITY];
    char response_buf[STREAM_QUEUE_CAPACITY];
};

#endif