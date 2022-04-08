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
// sockaddr_in
#include <netinet/in.h>
// inet_*
#include <arpa/inet.h>
// set errno
#include <errno.h>
// get random port number
#include <time.h>
#include "share_queue.h"
#include "share.h"
#include "queue.h"
#include "socket.h"

// origin function pointer
int (*original_socket)(int, int, int);
int (*original_bind)(int, const struct sockaddr *, socklen_t);
int (*original_listen)(int, int);
int (*original_accept)(int, struct sockaddr *, socklen_t *);
int (*original_connect)(int, const struct sockaddr *, socklen_t);
int (*original_close)(int);
int (*original_shutdown)(int, int);
int (*original_getsockname)(int, struct sockaddr *, socklen_t *);
ssize_t (*original_recv)(int, void *, size_t, int);
ssize_t (*original_send)(int, const void *, size_t, int);
int (*original_socketpair)(int, int, int, int [2]);
int (*original_getpeername)(int, struct sockaddr *, socklen_t *);

// queue to acquire available fd
Queue *available_fd;

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
    original_socketpair = dlsym(RTLD_NEXT, "socketpair");
    original_getpeername = dlsym(RTLD_NEXT, "getpeername");

    // initialize available fd queue
    available_fd = createQueue(1024);
    for(int i = 3; i <=1023; i++){
        enqueue(available_fd, i);
    }

    // initialize socket array
    memset(socket_arr, 0, 1024*sizeof(mysocket));

    // initialize random seed
    srand(time(NULL));

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
    share_queue req_queue = (share_queue){
        .front = -1,
        .rear = -1,
        .capacity = QUEUE_CAPACITY,
        .current_size = 0,
        .message_start_offset = 2*sizeof(share_queue)+2*sizeof(pthread_mutex_t)
    };
    memcpy(shm_ptr, &req_queue, sizeof(share_queue));
    request_queue = (share_queue *)shm_ptr;

    share_queue res_queue = (share_queue){
        .front = -1,
        .rear = -1,
        .capacity = QUEUE_CAPACITY,
        .current_size = 0,
        .message_start_offset = 2*sizeof(share_queue)+2*sizeof(pthread_mutex_t)+QUEUE_CAPACITY
    };
    memcpy(shm_ptr+sizeof(share_queue), &res_queue, sizeof(share_queue));
    response_queue = (share_queue *)(shm_ptr+sizeof(share_queue));

    //initialize mutex
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    request_lock = (pthread_mutex_t *)(shm_ptr+2*sizeof(share_queue));
    pthread_mutex_init(request_lock, &attr);
 
    response_lock = (pthread_mutex_t *)(shm_ptr+2*sizeof(share_queue)+sizeof(pthread_mutex_t));
    pthread_mutex_init(response_lock, &attr);
}

int is_valid_fd(int sockfd){
    if(sockfd >= 3 && sockfd <= 1023 && socket_arr[sockfd].in_use == 1)
        return 1;
    else
        return 0;
}

int socket(int domain, int type, int protocol){
    printf("hook socket()!\n");
    int fd = dequeue(available_fd);
    if(fd == INT_MIN){
        errno = EMFILE;
        return -1;
    }
    socket_arr[fd] = (mysocket){
        .domain = domain,
        .type = type,
        .protocol = protocol,
        .has_bind = 0,
        .in_use = 1
    };
    // return original_socket(domain, type, protocol);
    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    printf("hook bind()!\n");
    if(is_valid_fd(sockfd)){  
        // check addrlen is valid      
        if(addrlen > sizeof(struct sockaddr)){
            errno = ENAMETOOLONG;
            return -1;
        }
        socket_arr[sockfd].has_bind = 1;
        memcpy(&socket_arr[sockfd].addr, addr, addrlen);
    }
    else{
        errno = EBADF;
        return -1;
    }
    // return original_bind(sockfd, addr, addrlen);
    return 0;
}

int listen(int sockfd, int backlog){
    printf("hook listen()!\n");
    if(!is_valid_fd(sockfd)){
        errno = EBADF;
        return -1;
    }
    // return original_listen(sockfd, backlog);
    return 0;
}

int accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    printf("hook accept()!\n");
    int fd = 0;
    if(is_valid_fd(sockfd)){
        fd = dequeue(available_fd);
        if(fd == INT_MIN){
            errno = EMFILE;
            return -1;
        }
        socket_arr[fd] = (mysocket){
            .domain = socket_arr[sockfd].domain,
            .type = socket_arr[sockfd].type,
            .protocol = socket_arr[sockfd].protocol,
            .has_bind = 0,
            .in_use = 1
        };

        struct sockaddr_in *fake_addr = (struct sockaddr_in *) addr;
        struct in_addr net_addr;
        // suppose client use localhost to connect
        char fa[] = "127.0.0.1";
        inet_aton(fa, &net_addr);
        fake_addr->sin_addr = net_addr;
        // get random port for peer
        fake_addr->sin_port = rand() % 10000 + 10000;
        fake_addr->sin_family = AF_INET;
        // save peer_addr
        memcpy(&socket_arr[fd].peer_addr, fake_addr, sizeof(struct sockaddr));
    }
    else{
        errno = EBADF;
        return -1;
    }
    // return original_accept(sockfd, addr, addrlen);
    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    printf("hook connect()!\n");
    if(is_valid_fd(sockfd)){
        memcpy(&socket_arr[sockfd].peer_addr, addr, sizeof(struct sockaddr));
    }
    else{
        errno = EBADF;
        return -1;        
    }
    // return original_connect(sockfd, addr, addrlen);
    return 0;
}

int close(int fd){
    printf("hook close()!\n");
    if(is_valid_fd(fd)){
        memset(&socket_arr[fd], 0, sizeof(mysocket));
        enqueue(available_fd, fd);
    }
    else{
        errno = EBADF;
        return -1;        
    }
    // return original_close(fd);
    return 0;
}

int shutdown(int sockfd, int how){
    printf("hook shutdown()!\n");
    if(is_valid_fd(sockfd)){
        socket_arr[sockfd].is_shutdown = 1;
        socket_arr[sockfd].how_shutdown = how;
    }
    else{
        errno = EBADF;
        return -1;        
    }
    // return original_shutdown(sockfd, how);
    return 0;
}

int getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    printf("hook getsockname()!\n");
    if(is_valid_fd(sockfd)){
        memcpy(addr, &socket_arr[sockfd].addr, sizeof(struct sockaddr));
    }
    else{
        errno = EBADF;
        return -1;
    }
    // return original_getsockname(sockfd, addr, addrlen);
    return 0;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    // handle nonblocking flag
    if(flags & MSG_DONTWAIT){
        if(request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(request_queue->current_size == 0);

    if(pthread_mutex_lock(request_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    buffer *b = share_dequeue(shm_ptr, request_queue, len);
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
    count = share_enqueue(shm_ptr, response_queue, (char *)buf, len);
    if(pthread_mutex_unlock(response_lock) != 0) perror("pthread_mutex_unlock failed");
    
    return count;
}

int socketpair(int domain, int type, int protocol, int sv[2]){
    printf("hook socketpair()!\n");
    // return original_socketpair(domain, type, protocol, sv);
    return 0;
}

int getpeername(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    printf("hook getpeername()!\n");
    if(is_valid_fd(sockfd)){
        memcpy(addr, &socket_arr[sockfd].peer_addr, sizeof(struct sockaddr));
    }
    else{
        errno = EBADF;
        return -1;
    }
    // return original_getpeername(sockfd, addr, addrlen);
    return 0;
}