#ifndef _SOCKET_H_
#define _SOCKET_H_

#include <pthread.h>
#include "share_queue.h"
typedef struct mysocket mysocket;
#define LO_MSS 65496

struct mysocket {
    int domain;
    int type;
    int protocol;
    // check socket exist
    int in_use;
    // for getsockname
    int has_bind;
    struct sockaddr addr;
    // for getpeername
    struct sockaddr peer_addr;
    // for shutdown
    int shutdown_read;
    int shutdown_write;
    // for MSG_MORE
    // tcp
    char *msg_more_buf;
    int msg_more_size;
    // for share memory communication
    int share_unit_index;
    share_queue *request_queue;
    share_queue *response_queue;
    pthread_mutex_t *request_lock;
    pthread_mutex_t *response_lock;
    // GETFL and SETFL flags
    int file_status_flags;
};

#endif