#define _GNU_SOURCE
#include <sys/mman.h>
#include <fcntl.h> // O_* constant
#include <sys/stat.h> // mode constants
#include <stdio.h>
// socket
#include <sys/socket.h>
// dlsym
#include <dlfcn.h>
// memcpy
#include <string.h>
// exit
#include <stdlib.h>
// sleep
#include <unistd.h>
#include "queue.h"
#include "share.h"

// origin function pointer
int (*original_socket)(int, int, int);
int (*original_bind)(int, const struct sockaddr *, socklen_t);
int (*original_listen)(int, int);
int (*original_accept)(int, struct sockaddr *, socklen_t *);
int (*original_connect)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
int (*original_close)(int fd);
int (*original_shutdown)(int sockfd, int how);
int (*original_getsockname)(int sockfd, struct sockaddr *addr, socklen_t *addrlen);
ssize_t (*original_recv)(int, void *, size_t, int);
ssize_t (*original_send)(int, const void *, size_t, int);

__attribute__((constructor)) void init(){
    // initialize function pointer before main function
    original_socket = dlsym(RTLD_NEXT, "socket");
    original_bind = dlsym(RTLD_NEXT, "bind");
	original_listen = dlsym(RTLD_NEXT, "listen");
	original_accept = dlsym(RTLD_NEXT, "accept");
	original_connect = dlsym(RTLD_NEXT, "connect");
	original_close = dlsym(RTLD_NEXT, "close");
	original_shutdown = dlsym(RTLD_NEXT, "shutdown");
	original_getsockname = dlsym(RTLD_NEXT, "getsockname");
    original_recv = dlsym(RTLD_NEXT, "recv");
    original_send = dlsym(RTLD_NEXT, "send");

    // initialize share memory
    shm_fd = shm_open("message_sm", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        perror("shm_open failed");
        exit(1);
    }
    ftruncate(shm_fd, 0x400000);

    shm_ptr = mmap(NULL, 0x400000, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == (void *)-1){
        perror("mmap failed");
    }

    // initialize queue
    queue req_queue = (queue){
        .front = -1,
        .rear = -1,
        .capacity = QUEUE_CAPACITY,
        .current_size = 0,
        .message_start_offset = 2*sizeof(queue)+2*sizeof(pthread_mutex_t)
    };
    memcpy(shm_ptr, &req_queue, sizeof(queue));
    request_queue = (queue *)shm_ptr;

    queue res_queue = (queue){
        .front = -1,
        .rear = -1,
        .capacity = QUEUE_CAPACITY,
        .current_size = 0,
        .message_start_offset = 2*sizeof(queue)+2*sizeof(pthread_mutex_t)+QUEUE_CAPACITY
    };
    memcpy(shm_ptr+sizeof(queue), &res_queue, sizeof(queue));
    response_queue = (queue *)(shm_ptr+sizeof(queue));

    //initialize mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    // pthread_mutex_t req_lock;
    // memcpy(shm_ptr+2*sizeof(queue), &request_lock, sizeof(pthread_mutex_t));
    request_lock = (pthread_mutex_t *)(shm_ptr+2*sizeof(queue));
    pthread_mutex_init(request_lock, &attr);

    // pthread_mutex_t res_lock;
    // memcpy(shm_ptr+2*sizeof(queue)+sizeof(pthread_mutex_t), &response_lock, sizeof(pthread_mutex_t));   
    response_lock = (pthread_mutex_t *)(shm_ptr+2*sizeof(queue)+sizeof(pthread_mutex_t));
    pthread_mutex_init(response_lock, &attr);
}

int socket(int domain, int type, int protocol){
    printf("hook socket()!\n");
    // return original_socket(domain, type, protocol);
    return 999;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    printf("hook bind()!\n");
    // return original_bind(sockfd, addr, addrlen);
    return 0;
}

int listen(int sockfd, int backlog){
    printf("hook listen()!\n");
    // return original_listen(sockfd, backlog);
    return 0;
}

int accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    printf("hook accept()!\n");
    // return original_accept(sockfd, addr, addrlen);
    return 999;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    printf("hook connect()!\n");
    // return original_connect(sockfd, addr, addrlen);
    return 0;
}

int close(int fd){
    printf("hook close()!\n");
    // return original_close(fd);
    return 0;
}

int shutdown(int sockfd, int how){
    printf("hook shutdown()!\n");
    return original_shutdown(sockfd, how);
    // return 0;
}

int getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    printf("hook getsockname()!\n");
    return original_getsockname(sockfd, addr, addrlen);
    // return 0;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    while(request_queue->current_size == 0);

    if(pthread_mutex_lock(request_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    buffer *b = dequeue(shm_ptr, request_queue, len);
    if(b->buf != NULL){
        memcpy(buf+count, b->buf, b->length *sizeof(char));
        count = b->length;
        free(b->buf); 
    }
    free(b);
    if(pthread_mutex_unlock(request_lock) != 0) perror("pthread_mutex_unlock failed");

    return count;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags){
    while(response_queue->current_size == response_queue->capacity);

    if(pthread_mutex_lock(response_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    count = enqueue(shm_ptr, response_queue, (char *)buf, len);
    if(pthread_mutex_unlock(response_lock) != 0) perror("pthread_mutex_unlock failed");
    
    return count;
}