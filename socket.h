#ifndef _SOCKET_H_
#define _SOCKET_H_

#include <pthread.h>
#include <sys/time.h>
#include <time.h>
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
    // for aflnet response_bytes correction
    int response_su_index;
    share_queue *res_len_queue;
    pthread_mutex_t *res_queue_lock;
    // GETFL and SETFL flags
    int file_status_flags;
    // for dnsmasq
    int pollfds_index;
    // for poll
    int is_accept_fd;
    int is_server;
    // for socket timeout
    struct timeval send_timeout;
    struct timeval recv_timeout;
    timer_t send_timer;
    timer_t recv_timer;
    timer_t poll_timer;
    int is_socket_timeout;
};

#endif