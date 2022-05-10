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
// ceil
#include <math.h>
// hook function with variable-length argument
#include <stdarg.h>
// ioctl
#include <linux/sockios.h>
// struct ifreq
#include <net/if.h>
// struct arpreq
#include <net/if_arp.h>
// SIGPIPE
#include <signal.h>
// poll
#include <poll.h>
// timer in poll
#include <sys/timerfd.h>
#include <stdint.h>

#include "share_queue.h"
#include "queue.h"
#include "socket.h"
#include "share.h"
#include "stack.h"
#include "list.h"
// for logging
#include "log.h"

#define USE_DNSMASQ_POLL 1

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
ssize_t (*original_read)(int fd, void *buf, size_t count);
ssize_t (*original_write)(int fd, const void *buf, size_t count);
int (*original_setsockopt)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int (*original_getsockopt)(int sockfd, int level, int optname, void *restrict optval, socklen_t *restrict optlen);
int (*original_fcntl)(int fd, int cmd, ...);
int (*original_fcntl64)(int fd, int cmd, ...);
int (*original_ioctl)(int fd, unsigned long request, ...);
ssize_t (*original_recvfrom)(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);
ssize_t (*original_sendto)(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t (*original_recvmsg)(int socket, struct msghdr *message, int flags);
ssize_t (*original_sendmsg)(int socket, const struct msghdr *message, int flags);
int (*original_poll)(struct pollfd *fds, nfds_t nfds, int timeout);

// queue to acquire available fd
Stack *available_fd;
// queue to acquire available share_unit
Stack *available_share_unit;
// save information of self-management socket
mysocket socket_arr[1024];
// fd for logging
int log_fd;
struct timespec hook_start_time;

// struct for pthread_create argument
typedef struct args args;
struct args {
    struct pollfd *fds;
    nfds_t nfds;
    int timeout;
    int rv;
};

typedef struct hook_args hook_args;
struct hook_args {
    struct pollfd *fds;
    nfds_t nfds;
    int rv;
    pthread_mutex_t *mutex;
    pthread_cond_t *cond;
};

// for performance profiling
enum { NS_PER_SECOND = 1000000000 };

void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td)
{
    td->tv_nsec = t2.tv_nsec - t1.tv_nsec;
    td->tv_sec  = t2.tv_sec - t1.tv_sec;
    if (td->tv_sec > 0 && td->tv_nsec < 0)
    {
        td->tv_nsec += NS_PER_SECOND;
        td->tv_sec--;
    }
    else if (td->tv_sec < 0 && td->tv_nsec > 0)
    {
        td->tv_nsec -= NS_PER_SECOND;
        td->tv_sec++;
    }
}

// for debug
void my_log_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        log_trace("%02x", m[i]);
    }
}

// just compare family and port
int cmp_addr(const struct sockaddr *a, const struct sockaddr *b){
    struct sockaddr_in *ai = (struct sockaddr_in *)a;
    struct sockaddr_in *bi = (struct sockaddr_in *)b;

    if(ai->sin_family != bi->sin_family)
        return 0;
    else if(ai->sin_port != bi->sin_port)
        return 0;
    else
        return 1;
}

void init_share_queue(int i){
    //initialize mutex attr
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    share_queue req_queue = (share_queue){
        .front = -1,
        .rear = -1,
        .capacity = STREAM_QUEUE_CAPACITY,
        .current_size = 0,
        .message_start_offset = (i+1)*2*sizeof(share_queue)+(i+1)*2*sizeof(pthread_mutex_t)+i*2*STREAM_QUEUE_CAPACITY
    };
    memcpy(shm_ptr+i*sizeof(share_unit), &req_queue, sizeof(share_queue));

    share_queue res_queue = (share_queue){
        .front = -1,
        .rear = -1,
        .capacity = STREAM_QUEUE_CAPACITY,
        .current_size = 0,
        .message_start_offset = (i+1)*2*sizeof(share_queue)+(i+1)*2*sizeof(pthread_mutex_t)+(i*2+1)*STREAM_QUEUE_CAPACITY
    };
    memcpy(shm_ptr+i*sizeof(share_unit)+sizeof(share_queue), &res_queue, sizeof(share_queue));
    
    pthread_mutex_t *request_lock = (pthread_mutex_t *)(shm_ptr+i*sizeof(share_unit)+2*sizeof(share_queue));
    if(pthread_mutex_init(request_lock, &attr) != 0) log_error("pthread_mutex_init request lock failed");

    pthread_mutex_t *response_lock = (pthread_mutex_t *)(shm_ptr+i*sizeof(share_unit)+2*sizeof(share_queue)+sizeof(pthread_mutex_t));
    if(pthread_mutex_init(response_lock, &attr) != 0) log_error("pthread_mutex_init response lock failed");
}

void init_connect_accept_queue(void){
    connect_queue con_queue = (connect_queue){
        .front = 0,
        .rear = CONNECT_QUEUE_CAPACITY - 1,
        .size = 0,
        .capacity = CONNECT_QUEUE_CAPACITY,
        .queue_start_offset = sizeof(connect_queue)+sizeof(accept_queue)+2*sizeof(pthread_mutex_t)
    };
    connect_queue_ptr = (connect_queue *)connect_shm_ptr;
    memcpy(connect_queue_ptr, &con_queue, sizeof(connect_queue));

    accept_queue acc_queue = (accept_queue){
        .front = 0,
        .rear = ACCEPT_QUEUE_CAPACITY - 1,
        .size = 0,
        .capacity = ACCEPT_QUEUE_CAPACITY,
        .queue_start_offset = sizeof(connect_queue)+sizeof(accept_queue)+2*sizeof(pthread_mutex_t)+CONNECT_QUEUE_CAPACITY*sizeof(connection)
    };
    accept_queue_ptr = (accept_queue *)(connect_shm_ptr+sizeof(connect_queue));
    memcpy(accept_queue_ptr, &acc_queue, sizeof(accept_queue));

    // initialize mutex lock for connect queue and accept queue
    //initialize mutex attr
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    
    connect_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue));
    if(pthread_mutex_init(connect_lock, &attr) != 0) log_error("pthread_mutex_init connect_lock failed");
    
    accept_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue)+sizeof(pthread_mutex_t));
    if(pthread_mutex_init(accept_lock, &attr) != 0) log_error("pthread_mutex_init accept_lock failed");
}

int init_socket(int fd, int domain, int type, int protocol){
    socket_arr[fd] = (mysocket){
        .domain = domain,
        .type = type,
        .protocol = protocol,
        .has_bind = 0,
        .in_use = 1,
        .shutdown_read = 0,
        .shutdown_write = 0,
        .msg_more_buf = NULL,
        .msg_more_size = 0,
        .share_unit_index = -1,
        .file_status_flags = 0,
        .pollfds_index = -1,
        .is_accept_fd = 0,
        .is_server = 0
    };
    socket_arr[fd].share_unit_index = pop(available_share_unit);
    if(socket_arr[fd].share_unit_index == INT_MIN){
        log_fatal("need to increase SOCK_NUM\n");
        return -1;
    }
    init_share_queue(socket_arr[fd].share_unit_index);
    socket_arr[fd].request_queue = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].request_queue);
    socket_arr[fd].response_queue = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].response_queue);
    socket_arr[fd].request_lock = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].request_lock);
    socket_arr[fd].response_lock = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].response_lock);
    return 0;
}

int init_close_unit(int share_unit_index){
    if(share_unit_index < 0 || share_unit_index >= SOCKET_NUM){
        log_error("share unit index out of bound\n");
        return -1;
    }

    close_arr[share_unit_index].client_read = 0;
    close_arr[share_unit_index].client_write = 0;
    close_arr[share_unit_index].server_read = 0;
    close_arr[share_unit_index].server_write = 0;

    return 0;
}

__attribute__((constructor)) void init(){
    clock_gettime(CLOCK_REALTIME, &hook_start_time);
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
    original_read = dlsym(RTLD_NEXT, "read");
    original_write = dlsym(RTLD_NEXT, "write");
    original_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    original_getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    original_fcntl = dlsym(RTLD_NEXT, "fcntl");
    original_fcntl64 = dlsym(RTLD_NEXT, "fcntl64");
    original_ioctl = dlsym(RTLD_NEXT, "ioctl");
    original_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    original_sendto = dlsym(RTLD_NEXT, "sendto");
    original_recvmsg = dlsym(RTLD_NEXT, "recvmsg");
    original_sendmsg = dlsym(RTLD_NEXT, "sendmsg");
    original_poll = dlsym(RTLD_NEXT, "poll");

    // initialize logging
    log_set_quiet(true);
    log_fd = -2;
    char *log_name = getenv("socket_hook_log");
    if(log_name){
        FILE *fp = fopen((const char *)log_name, "w+");
        if(fp && fileno(fp) != -1){
            log_fd = fileno(fp);
            log_add_fp(fp, LOG_ERROR);
        }
    }
    //log_debug("attribute init");
    // initialize available fd stack
    available_fd = createStack(1024);
    for(int i = 0; i <=1023; i++){
        push(available_fd, i);
    }

    // initialize available share unit queue
    available_share_unit = createStack(SOCKET_NUM);
    for(int i = SOCKET_NUM - 1; i >= 0; i--){
        push(available_share_unit, i);
    }

    // initialize socket array
    memset(socket_arr, 0, 1024*sizeof(mysocket));

    // initialize random seed
    srand(time(NULL));

    // initialize communication share memory
    shm_fd = shm_open("message_sm", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        log_error("message_sm shm_open failed");
        exit(999);
    }
    ftruncate(shm_fd, COMMUNICATE_SHM_SIZE);

    shm_ptr = mmap(NULL, COMMUNICATE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == (void *)-1){
        log_error("shm_ptr mmap failed");
    }

    // initialize connect share memory
    connect_shm_fd = shm_open("connect_sm", O_CREAT | O_RDWR, 0666);
    if (connect_shm_fd < 0){
        log_error("connect_sm shm_open failed");
        exit(999);
    }
    ftruncate(connect_shm_fd, CONNECT_SHM_SIZE);

    connect_shm_ptr = mmap(NULL, CONNECT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, connect_shm_fd, 0);
    if(connect_shm_ptr == (void *)-1){
        log_error("connect_shm_ptr mmap failed");
    }
    init_connect_accept_queue();

    // initialize request/response queue
    for(int i = 0; i < SOCKET_NUM; i++){
        init_share_queue(i);
    }

    // initialize close share memory
    close_shm_fd = shm_open("close_sm", O_CREAT | O_RDWR, 0666);
    if (close_shm_fd < 0){
        log_error("close_shm_fd shm_open failed");
        exit(999);
    }
    ftruncate(close_shm_fd, CLOSE_SHM_SIZE);

    close_shm_ptr = mmap(NULL, CLOSE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, close_shm_fd, 0);
    if(close_shm_ptr == (void *)-1){
        log_error("close_shm_ptr mmap failed");
    }
    memset(close_shm_ptr, 0, CLOSE_SHM_SIZE);
    close_arr = (close_unit *)close_shm_ptr;
}

int is_valid_fd(int sockfd){
    if(sockfd >= 0 && sockfd <= 1023 && socket_arr[sockfd].in_use == 1)
        return 1;
    else
        return 0;
}

int socket(int domain, int type, int protocol){
    // filter out non-tcp and unix socket for dnsmasq
    if(!(domain == AF_INET && type == SOCK_STREAM))
        return original_socket(domain, type, protocol);

    struct timespec start, finish, delta;
    clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook socket()!");
    int fd = pop(available_fd);
    if(fd == INT_MIN){
        errno = EMFILE;
        log_error("pop fd failed");
        return -1;
    }
    if (init_socket(fd, domain, type, protocol) == -1){
        log_error("init socket failed, fd=%d", fd);
        return -1;
    }
    log_trace("use self management fd: %d", fd);
    clock_gettime(CLOCK_REALTIME, &finish);
    sub_timespec(start, finish, &delta);
    log_info("socket time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    if(!is_valid_fd(sockfd)) 
        return original_bind(sockfd, addr, addrlen);

    log_trace("hook bind(), fd=%d", sockfd);
    // check addrlen is valid      
    if(addrlen > sizeof(struct sockaddr)){
        errno = ENAMETOOLONG;
        log_error("bind addrlen > sizeof(sturct sockaddr)");
        return -1;
    }
    socket_arr[sockfd].has_bind = 1;
    memcpy(&socket_arr[sockfd].addr, addr, addrlen);

    return 0;
}

int listen(int sockfd, int backlog){
    log_trace("hook listen(), fd=%d", sockfd);
    if(!is_valid_fd(sockfd)){
        return original_listen(sockfd, backlog);
    }
    // for poll
    socket_arr[sockfd].is_accept_fd = 1;

    return 0;
}

int accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    if(!is_valid_fd(sockfd))
        return original_accept(sockfd, addr, addrlen);
    
    struct timespec start, finish, delta;
    int fd = 0;    
    clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook accept(), fd=%d", sockfd);
    if(socket_arr[sockfd].file_status_flags & O_NONBLOCK){
        if(connect_queue_ptr->size == 0){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    fd = pop(available_fd);
    if(fd == INT_MIN){
        errno = EMFILE;
        return -1;
    }
    if(init_socket(fd, socket_arr[sockfd].domain, socket_arr[sockfd].type, socket_arr[sockfd].protocol) == -1)
        return -1;
    socket_arr[fd].is_server = 1;

    // save bind address to share memory
    if(socket_arr[sockfd].has_bind == 1 && socket_arr[fd].share_unit_index >= 0 && socket_arr[fd].share_unit_index < SOCKET_NUM){
        while(connect_queue_ptr->size == 0)
            usleep(1);
        if(pthread_mutex_lock(connect_lock) != 0) log_error("pthread_mutex_lock connect_lock failed");
        connection *c = Connect_dequeue(connect_shm_ptr, connect_queue_ptr);
        if(pthread_mutex_unlock(connect_lock) != 0) log_error("pthread_mutex_unlock connect_lock failed");
        if(c != NULL){
            // accept connection and save share unit index to accept queue
            if(cmp_addr(&socket_arr[sockfd].addr, &(c->addr))){
                acception a = (acception){
                    .client_fd = c->client_fd,
                    .share_unit_index = socket_arr[fd].share_unit_index
                };
                
                // init close unit before client accept
                init_close_unit(socket_arr[fd].share_unit_index);
                while(accept_queue_ptr->size == accept_queue_ptr->capacity)
                    usleep(1);
                if(pthread_mutex_lock(accept_lock) != 0) log_error("pthread_mutex_lock accept_lock failed");
                if(Accept_enqueue(connect_shm_ptr, accept_queue_ptr, a) == -1)
                    log_error("accept queue enqueue failed");
                if(pthread_mutex_unlock(accept_lock) != 0) log_error("pthread_mutex_unlock accept_lock failed");
            }
            free(c);
        }
        else{
            log_error("connect queue dequeue failed");
            return -1;
        }
    }
    else{
        log_error("not bind or share unit index out of bound!");
        return -1;
    }

    struct sockaddr_in *fake_addr;
    if(addr != NULL)
        fake_addr = (struct sockaddr_in *) addr;
    else
        fake_addr = (struct sockaddr_in *)malloc(sizeof(struct sockaddr_in));
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
    if(addr == NULL)
        free(fake_addr); 

    clock_gettime(CLOCK_REALTIME, &finish);
    sub_timespec(start, finish, &delta);
    log_info("accept time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    if(!is_valid_fd(sockfd))
        return original_connect(sockfd, addr, addrlen);
        
    log_trace("hook connect(), fd=%d", sockfd);
    memcpy(&socket_arr[sockfd].peer_addr, addr, sizeof(struct sockaddr));

    return 0;
}

int close(int fd){
    if(fd == log_fd){
        log_trace("hook close(), log_fd=%d",fd);
        return 0;
    }

    if(!is_valid_fd(fd))
        return original_close(fd);

    log_trace("hook close(), fd=%d",fd);
    struct timespec start, finish, delta;
    clock_gettime(CLOCK_REALTIME, &start);

    push(available_share_unit, socket_arr[fd].share_unit_index);
    // clear address in connect sockaddr array
    init_connect_accept_queue();
    if(socket_arr[fd].share_unit_index >= 0){
        close_arr[socket_arr[fd].share_unit_index].server_read = 1;
        close_arr[socket_arr[fd].share_unit_index].server_write = 1;
    }
    memset(&socket_arr[fd], 0, sizeof(mysocket));
    push(available_fd, fd);

    clock_gettime(CLOCK_REALTIME, &finish);
    sub_timespec(start, finish, &delta);
    log_info("close time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    return 0;   
}

int shutdown(int sockfd, int how){
    if(!is_valid_fd(sockfd))
        return original_shutdown(sockfd, how);

    log_trace("hook shutdown(), fd=%d", sockfd);
    if(how == SHUT_RD){
        socket_arr[sockfd].shutdown_read = 1;
        if(socket_arr[sockfd].share_unit_index >= 0)
            close_arr[socket_arr[sockfd].share_unit_index].server_read = 1;
    }
    else if(how == SHUT_WR){
        socket_arr[sockfd].shutdown_write = 1;
        if(socket_arr[sockfd].share_unit_index >= 0)
            close_arr[socket_arr[sockfd].share_unit_index].server_write = 1;
    }
    else if(how == SHUT_RDWR){
        socket_arr[sockfd].shutdown_read = 1;
        socket_arr[sockfd].shutdown_write = 1;
        if(socket_arr[sockfd].share_unit_index >= 0){
            close_arr[socket_arr[sockfd].share_unit_index].server_read = 1;
            close_arr[socket_arr[sockfd].share_unit_index].server_write = 1;
        }
    }   

    return 0;
}

int getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    if(!is_valid_fd(sockfd))
        return original_getsockname(sockfd, addr, addrlen);

    log_trace("hook getsockname(), fd=%d", sockfd);
    memcpy(addr, &socket_arr[sockfd].addr, sizeof(struct sockaddr));   

    return 0;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    if(!is_valid_fd(sockfd))
        return original_recv(sockfd, buf, len, flags);

    log_trace("hook recv(), fd=%d", sockfd);
    // deal with shutdown
    if(socket_arr[sockfd].shutdown_read || close_arr[socket_arr[sockfd].share_unit_index].client_write)
        return 0;

    // handle nonblocking flag
    if(flags & MSG_DONTWAIT || socket_arr[sockfd].file_status_flags & O_NONBLOCK){
        if(socket_arr[sockfd].request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    
    ssize_t count = 0;
    // while loop for MSG_WAITALL
    while(count != len){
        while(socket_arr[sockfd].request_queue->current_size == 0){
            if(close_arr[socket_arr[sockfd].share_unit_index].client_write)
                return 0;
            usleep(1);
        }

        if(pthread_mutex_lock(socket_arr[sockfd].request_lock) != 0) log_error("pthread_mutex_lock request_lock failed");
        if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_STREAM){
            if(flags & MSG_PEEK){
                char *m_arr = (char *)(shm_ptr+socket_arr[sockfd].request_queue->message_start_offset);
                memcpy(buf+count, &m_arr[socket_arr[sockfd].request_queue->front], min(socket_arr[sockfd].request_queue->current_size,len-count)*sizeof(char));
                count += min(socket_arr[sockfd].request_queue->current_size,len-count);
            }
            else{
                buffer *b = stream_dequeue(shm_ptr, socket_arr[sockfd].request_queue, len-count);
                if(b->buf != NULL){
                    // tcp will discard the received byte, rather than save in buffer
                    if(!(flags & MSG_TRUNC)){
                        memcpy(buf+count, b->buf, b->length *sizeof(char));
                    }
                    count += b->length;
                    free(b->buf);
                }
                free(b);
            }
        }
        // in other case, MSG_TRUNC return the real length of the packet and discard the oversized part
        else if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_DGRAM){
            message_t *m = datagram_dequeue(shm_ptr, socket_arr[sockfd].request_queue);
            if(m->length != 0){
                memcpy(buf, m->buf, min(len,m->length));
                count = m->length;
            }
            free(m);
        }
        else{
            log_fatal("not implement socket type in recv!");
            exit(999);
        }
        if(pthread_mutex_unlock(socket_arr[sockfd].request_lock) != 0) log_error("pthread_mutex_unlock request lock failed");
        // skip if no MSG_WAITALL or socket type is datagram (MSG_WAITALL has no effect)
        if(!(flags & MSG_WAITALL) || (socket_arr[sockfd].type == SOCK_DGRAM)) break;
    }

    return count;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags){
    if(!is_valid_fd(sockfd))
        return original_send(sockfd, buf, len, flags);

    log_trace("hook send(), fd=%d", sockfd);
    // deal with shutdown
    if(socket_arr[sockfd].shutdown_write)
        raise(SIGPIPE);

    // handle nonblocking flag
    if(flags & MSG_DONTWAIT || socket_arr[sockfd].file_status_flags & O_NONBLOCK){
        if(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity){
        if(close_arr[socket_arr[sockfd].share_unit_index].client_read)
            raise(SIGPIPE);
        usleep(1);
    }

    ssize_t count = 0;
    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
    if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_STREAM){
        if((flags & MSG_MORE) && (socket_arr[sockfd].msg_more_size + len < LO_MSS)){
            // save stream in temporary buffer
            if(socket_arr[sockfd].msg_more_buf == NULL){
                socket_arr[sockfd].msg_more_buf = malloc(len*sizeof(char));
                socket_arr[sockfd].msg_more_size = len;
                memcpy(socket_arr[sockfd].msg_more_buf, buf, len);
                count = len;
            }
            // temporary buffer already exist
            else{
                socket_arr[sockfd].msg_more_buf = realloc(socket_arr[sockfd].msg_more_buf, (socket_arr[sockfd].msg_more_size+len)*sizeof(char));
                memcpy(socket_arr[sockfd].msg_more_buf+socket_arr[sockfd].msg_more_size, buf, len);
                socket_arr[sockfd].msg_more_size += len;
                count = len;
            }
        }
        // temporary buffer length might exceed MSS, force to send stream
        else if(socket_arr[sockfd].msg_more_buf != NULL){
            int total_len = socket_arr[sockfd].msg_more_size + len;
            int send_times = (int)(total_len / LO_MSS) + 1;
            char *temp_buf = malloc(total_len*sizeof(char));
            memcpy(temp_buf, socket_arr[sockfd].msg_more_buf, socket_arr[sockfd].msg_more_size);
            memcpy(temp_buf+socket_arr[sockfd].msg_more_size, buf, len);

            int temp_count = 0;
            for(int i = 0; i < send_times; i++){
                if(i == 0){
                    temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf, min(total_len,LO_MSS));
                    if(temp_count >= 0)
                        count = max((temp_count - socket_arr[sockfd].msg_more_size), 0);
                    if(send_times == 1){
                        if(temp_count < 0)
                            count = temp_count;
                        break;
                    }    
                }
                else{
                    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity)
                        usleep(1);

                    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
                    if(i == send_times - 1) 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf+i*LO_MSS, total_len-i*LO_MSS);                   
                    else 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf+i*LO_MSS, LO_MSS);
                    
                    if(temp_count > 0)
                        count += temp_count;
                }                

                if(i != send_times - 1){
                    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
                }             
            }
            // clean temporary buffer
            free(socket_arr[sockfd].msg_more_buf);
            socket_arr[sockfd].msg_more_buf = NULL;
            socket_arr[sockfd].msg_more_size = 0;
            free(temp_buf);
        }
        // no temporary buffer
        else{
            int send_times = (int)(len / LO_MSS) + 1;
            int temp_count = 0;
            for(int i = 0; i < send_times; i++){
                if(i == 0){
                    temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf, min(len,LO_MSS));
                    if(temp_count > 0)
                        count = temp_count;
                    if(send_times == 1){
                        if(temp_count < 0)
                            count = temp_count;
                        break;
                    }   
                }
                else{
                    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity)
                        usleep(1);

                    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
                    if(i == send_times - 1) 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf+i*LO_MSS, len-i*LO_MSS);                   
                    else 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf+i*LO_MSS, LO_MSS);
                    
                    if(temp_count > 0)
                        count += temp_count;
                }                

                if(i != send_times - 1){
                    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
                }             
            }
        }
    }
    else if(socket_arr[sockfd].domain == AF_INET && socket_arr[sockfd].type == SOCK_DGRAM){
        message_t m = {
            .length = len,
        };
        memcpy(m.buf, buf, len);
        if(datagram_enqueue(shm_ptr, socket_arr[sockfd].response_queue, m) == 0)
            count = len;
    }
    else{
        log_fatal("not implement socket type in send!");
        exit(999);
    }
    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
    
    return count;
}

int socketpair(int domain, int type, int protocol, int sv[2]){
    // filter out non-tcp and unix socket (dnsmasq not use this function)
    if(domain == AF_UNIX || type != SOCK_STREAM)
        return original_socketpair(domain, type, protocol, sv);

    log_trace("hook socketpair()!");
    int fd1 = pop(available_fd);
    if(fd1 == INT_MIN){
        errno = EMFILE;
        return -1;
    }
    if (init_socket(fd1, domain, type, protocol) == -1)
        return -1;

    int fd2 = pop(available_fd);
    if(fd2 == INT_MIN){
        memset(&socket_arr[fd1], 0, sizeof(mysocket));
        push(available_fd, fd1);
        errno = EMFILE;
        return -1;
    }
    if (init_socket(fd2, domain, type, protocol) == -1)
        return -1;

    sv[0] = fd1;
    sv[1] = fd2; 

    return 0;
}

int getpeername(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    if(!is_valid_fd(sockfd))
        return original_getpeername(sockfd, addr, addrlen);
        
    log_trace("hook getpeername(), fd=%d", sockfd);
    memcpy(addr, &socket_arr[sockfd].peer_addr, sizeof(struct sockaddr));
    log_trace("getpeername return 0");
    return 0;
}

ssize_t read(int fd, void *buf, size_t count){
    if(!is_valid_fd(fd))
        return original_read(fd, buf, count);

    struct timespec start, finish, delta;
    clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook read(), fd=%d, count=%ld", fd, count);
    // deal with shutdown
    if(socket_arr[fd].shutdown_read || close_arr[socket_arr[fd].share_unit_index].client_write){
        log_trace("read shutdown");
        return 0;
    }

    // handle nonblocking flag
    if(socket_arr[fd].file_status_flags & O_NONBLOCK){
        //log_trace("non-blocking read");
        if(socket_arr[fd].request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            log_trace("read EWOULDBLOCK");
            return -1;
        }
    }
    log_trace("start blocking");
    while(__sync_bool_compare_and_swap(&socket_arr[fd].request_queue->current_size, 0, 0)){
        if(__sync_fetch_and_add(&close_arr[socket_arr[fd].share_unit_index].client_write, 0)){
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            log_info("read (client close) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            //kill(getpid(), SIGKILL);
            return 0;
        }
        usleep(1);
    }
    log_trace("start get lock");
    if(pthread_mutex_lock(socket_arr[fd].request_lock) != 0) log_error("pthread_mutex_lock request_lock failed");
    ssize_t my_count = 0;
    buffer *b = stream_dequeue(shm_ptr, socket_arr[fd].request_queue, count);
    if(b->buf != NULL){
        memcpy(buf, b->buf, b->length *sizeof(char));
        my_count = b->length;
        free(b->buf);
    }
    free(b);
    if(pthread_mutex_unlock(socket_arr[fd].request_lock) != 0) log_error("pthread_mutex_unlock request_lock failed");
    // my_log_hex(buf, my_count);
    log_trace("read return %ld", my_count);
    clock_gettime(CLOCK_REALTIME, &finish);
    sub_timespec(start, finish, &delta);
    log_info("read (normal) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    return my_count;
}

ssize_t write(int fd, const void *buf, size_t count){
    if(!is_valid_fd(fd))
        return original_write(fd, buf, count);
    
    struct timespec start, finish, delta;
    clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook write(), fd=%d, count=%ld", fd, count);
    // my_log_hex(buf, count);
    // deal with shutdown
    if(socket_arr[fd].shutdown_write)
        raise(SIGPIPE);

    // handle nonblocking flag
    if(socket_arr[fd].file_status_flags & O_NONBLOCK){
        if(socket_arr[fd].response_queue->current_size == socket_arr[fd].response_queue->capacity){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(socket_arr[fd].response_queue->current_size == socket_arr[fd].response_queue->capacity){
        if(close_arr[socket_arr[fd].share_unit_index].client_read){
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            log_info("write (client close) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            log_trace("write raise SIGPIPE");
            raise(SIGPIPE);
        }
        usleep(1);
    }

    if(pthread_mutex_lock(socket_arr[fd].response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
    ssize_t my_count = 0;
    my_count = stream_enqueue(shm_ptr, socket_arr[fd].response_queue, (char *)buf, count);
    if(pthread_mutex_unlock(socket_arr[fd].response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");
    clock_gettime(CLOCK_REALTIME, &finish);  
    sub_timespec(hook_start_time, finish, &delta);
    log_info("write relative time: %lld.%.9ld", delta.tv_sec, delta.tv_nsec);
    log_info("write clock time: %lld.%.9ld", finish.tv_sec, finish.tv_nsec);
    sub_timespec(start, finish, &delta);
    log_info("write (normal) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    return my_count;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen){
    if(!is_valid_fd(sockfd))
        return original_setsockopt(sockfd, level, optname, optval, optlen);

    log_trace("hook setsockopt(), fd=%d", sockfd);
    return 0;
}

int getsockopt(int sockfd, int level, int optname, void *restrict optval, socklen_t *restrict optlen){
    if(!is_valid_fd(sockfd))
        return original_getsockopt(sockfd, level, optname, optval, optlen);
    
    log_trace("hook getsockopt(), fd=%d", sockfd);
    return 0;
}

int fcntl(int fd, int cmd, ...){
    log_trace("hook fcntl(), fd=%d", fd);
    va_list ap;
    va_start(ap, cmd);
    int flags;
    if(!is_valid_fd(fd)){
        switch(cmd){
            case F_GETFD:
                va_end(ap);
                return original_fcntl(fd, cmd);
            case F_SETFD:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl(fd, cmd, flags);
            case F_GETFL:
                va_end(ap);
                return original_fcntl(fd, cmd);
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl(fd, cmd, flags);
            default:
                va_end(ap);
                log_fatal("fcntl cmd not implement yet");
                exit(999);
        }
    }
    else{
        switch(cmd){
            case F_GETFL:
                va_end(ap);
                log_trace("cmd=F_GETFL, return %d", socket_arr[fd].file_status_flags);
                return socket_arr[fd].file_status_flags;
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                socket_arr[fd].file_status_flags = flags;
                log_trace("cmd=F_SETFL, return %d", socket_arr[fd].file_status_flags);
                return 0;
            default:
                va_end(ap);
                log_fatal("fcntl cmd not implement yet");
                exit(999);
        }
    }
}

int fcntl64(int fd, int cmd, ...){
    va_list ap;
    va_start(ap, cmd);
    int flags;
    if(!is_valid_fd(fd)){
        switch(cmd){
            case F_GETFD:
                va_end(ap);
                return original_fcntl64(fd, cmd);
            case F_SETFD:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl64(fd, cmd, flags);
            case F_GETFL:
                va_end(ap);
                return original_fcntl64(fd, cmd);
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                return original_fcntl64(fd, cmd, flags);
            default:
                va_end(ap);
                log_fatal("fcntl64 cmd not implement yet");
                exit(999);
        }
    }
    else{
        log_trace("hook fcntl64(), fd=%d", fd);
        switch(cmd){
            case F_GETFL:
                va_end(ap);
                log_trace("cmd=F_GETFL, return %d", socket_arr[fd].file_status_flags);
                return socket_arr[fd].file_status_flags;
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                socket_arr[fd].file_status_flags = flags;
                log_trace("cmd=F_SETFL, return %d", socket_arr[fd].file_status_flags);
                return 0;
            default:
                va_end(ap);
                log_fatal("fcntl64 cmd not implement yet");
                exit(999);
        }
    }
}

int ioctl(int fd, unsigned long request, ...){
    va_list ap;
    va_start(ap, request);
    if(!is_valid_fd(fd)){
        struct ifreq *ifr;
        struct timeval *tv;
        struct arpreq *arp;
        switch(request){
            case SIOCGIFNAME:
            case SIOCGIFFLAGS:
            case SIOCGIFADDR:
            case SIOCGIFMTU:
            case SIOCGIFINDEX:
                ifr = va_arg(ap, struct ifreq*);
                va_end(ap);
                return original_ioctl(fd, request, ifr);
            case SIOCGSTAMP:
                tv = va_arg(ap, struct timeval*);
                va_end(ap);
                return original_ioctl(fd, request, tv);
            case SIOCSARP:
                arp = va_arg(ap, struct arpreq*);
                va_end(ap);
                return original_ioctl(fd, request, arp);
        }
    }
    log_trace("hook ioctl(), fd=%d", fd);
    va_end(ap);
    return 0;
}

ssize_t recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len){
    if(!is_valid_fd(socket))
        return original_recvfrom(socket, buffer, length, flags, address, address_len);
    else{
        log_trace("hook recvfrom()!");
        log_fatal("recvfrom not implement this part");
        exit(999);
    }
}

ssize_t sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len){
    if(!is_valid_fd(socket))
        return original_sendto(socket, message, length, flags, dest_addr, dest_len);
    else{
        log_trace("hook sendto()!");
        log_fatal("sendto not implement this part");
        exit(999);
    }
}

ssize_t recvmsg(int socket, struct msghdr *message, int flags){
    if(!is_valid_fd(socket))
        return original_recvmsg(socket, message, flags);
    else{
        log_trace("hook recvmsg()!");
        log_fatal("recvmsg not implement this part");
        exit(999);
    }
}

ssize_t sendmsg(int socket, const struct msghdr *message, int flags){
    if(!is_valid_fd(socket))
        return original_sendmsg(socket, message, flags);
    else{
        log_trace("hook sendmsg()!");
        log_fatal("sendmsg not implement this part");
        exit(999);
    }
}

// poll result for hook fd
int hook_fd_poll(struct pollfd *fds, nfds_t nfds){
    int rv = 0;
    int fd;
    for(int i = 0; i < nfds; i++){
        // clear revents
        fds[i].revents = 0;
        fd = fds[i].fd;
        // check POLLIN meaning
        if(socket_arr[fd].is_accept_fd){
            if((fds[i].events & POLLIN) && connect_queue_ptr->size > 0){
                fds[i].revents |= POLLIN;
                rv++;
            }
        }
        else{
            if(fds[i].events & POLLIN){
                // check connection direction
                if((socket_arr[fd].is_server && socket_arr[fd].request_queue->current_size > 0) || (!socket_arr[fd].is_server && socket_arr[fd].response_queue->current_size > 0)){
                    fds[i].revents |= POLLIN;
                    rv++;
                }
            }
            else if(fds[i].events & POLLOUT){
                // check connection direction
                if((socket_arr[fd].is_server && socket_arr[fd].response_queue->current_size < socket_arr[fd].response_queue->capacity) || (!socket_arr[fd].is_server && socket_arr[fd].request_queue->current_size < socket_arr[fd].request_queue->capacity)){
                    fds[i].revents |= POLLOUT;
                    rv++; 
                }
            }
        }
    }
    return rv;
}

void *call_poll(void *argument){
    args *poll_arg = (args *)argument;

    int oldtype;
    /* allow the thread to be killed at any time */
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);

    poll_arg->rv = original_poll(poll_arg->fds, poll_arg->nfds, poll_arg->timeout);

    pthread_exit(NULL);
    return NULL;
}

void *call_hook_fd_poll(void *argument){
    hook_args *hook_poll_arg = (hook_args *)argument;

    int oldtype;
    /* allow the thread to be killed at any time */
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, &oldtype);
    while(hook_poll_arg->rv == 0)
        hook_poll_arg->rv = hook_fd_poll(hook_poll_arg->fds, hook_poll_arg->nfds);

    pthread_cond_signal(hook_poll_arg->cond);
    return NULL;
}

nfds_t get_hook_nfds(struct pollfd *fds, nfds_t nfds){
    nfds_t hook_nfds = 0;
    for(int i = 0; i < nfds; i++){
        if(is_valid_fd(fds[i].fd))
            hook_nfds++;
    }
    return hook_nfds;
}

int poll(struct pollfd *fds, nfds_t nfds, int timeout){
    struct timespec start, finish, delta;
    clock_gettime(CLOCK_REALTIME, &start);
    log_trace("hook poll()!");
    int rv = 0, hook_rv = 0;
    struct timespec abs_time;
    int err;
    pthread_t tid;
    // save nfds for hook fd in poll
    nfds_t hook_nfds = get_hook_nfds(fds, nfds);
    // log_debug("hook_nfds: %ld", hook_nfds);

    if(!USE_DNSMASQ_POLL){
        log_fatal("not implement yet");
        exit(999);
    }

    if(timeout == 0){
        if(nfds-hook_nfds > 0)
            rv = original_poll(fds, nfds-hook_nfds, timeout);
        // log_debug("poll rv: %d", rv);
        if(rv == -1)
            return rv;

        if(hook_nfds > 0)
            hook_rv = hook_fd_poll(&fds[nfds-hook_nfds], hook_nfds);
        // log_debug("hook_rv: %d", hook_rv);
        return rv+hook_rv;
    }
    
    // timeout > 0 or timeout < 0
    if(nfds-hook_nfds > 0){   
        args poll_arg = (args) {
            .fds = fds,
            .nfds = nfds-hook_nfds,
            .timeout = timeout,
            .rv = 0
        };
        
        pthread_create(&tid, NULL, call_poll, (void *)&poll_arg);
        do {
            if(hook_nfds > 0)
                hook_rv = hook_fd_poll(&fds[nfds-hook_nfds], hook_nfds);
            if(hook_rv > 0){
                // log_debug("hook_rv: %d", hook_rv);
                if(pthread_tryjoin_np(tid, NULL) != 0){
                    // log_debug("normal poll blocking");
                    pthread_cancel(tid);
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("poll (hook+ n-) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    return hook_rv;
                }
                    
                if(poll_arg.rv == -1){
                    clock_gettime(CLOCK_REALTIME, &finish);
                    sub_timespec(start, finish, &delta);
                    log_info("poll (n error) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                    return -1;
                }
                // log_debug("poll rv: %d", poll_arg.rv);
                clock_gettime(CLOCK_REALTIME, &finish);
                sub_timespec(start, finish, &delta);
                log_info("poll (hook+ n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
                return hook_rv+poll_arg.rv;
            }
            usleep(1);
        } while(pthread_tryjoin_np(tid, NULL) != 0);

        if(poll_arg.rv == -1){
            clock_gettime(CLOCK_REALTIME, &finish);
            sub_timespec(start, finish, &delta);
            log_info("poll (join n-) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
            return -1;
        }
        // log_debug("hook_rv: %d", hook_rv);
        // log_debug("poll rv: %d", poll_arg.rv);
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("poll (join hook+ n+) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
        return hook_rv+poll_arg.rv;
    }

    // no original poll
    if(timeout == -1){
        // log_debug("timeout is -1");
        while(!hook_rv)
            hook_rv = hook_fd_poll(&fds[nfds-hook_nfds], hook_nfds);

        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("poll (only hook fd infinite loop) time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
        return hook_rv;
    }
    
    // no original poll and timeout > 0
    pthread_mutex_t polling = PTHREAD_MUTEX_INITIALIZER;
    pthread_cond_t done = PTHREAD_COND_INITIALIZER;
    hook_args hook_poll_arg = (hook_args) {
        .fds = fds,
        .nfds = hook_nfds,
        .rv = 0,
        .mutex = &polling,
        .cond = &done
    };
    
    //clock_gettime(CLOCK_REALTIME, &abs_time);
    abs_time.tv_sec += (time_t)(timeout / 1000);
    abs_time.tv_nsec += (long)(timeout % 1000) * 1000000;
    pthread_mutex_lock(&polling);

    pthread_create(&tid, NULL, call_hook_fd_poll, (void *)&hook_poll_arg);
    err = pthread_cond_timedwait(&done, &polling, &abs_time);
    pthread_mutex_unlock(&polling);
    
    if(err == ETIMEDOUT){
        // printf("pthread timeout\n");
        pthread_cancel(tid);
        clock_gettime(CLOCK_REALTIME, &finish);
        sub_timespec(start, finish, &delta);
        log_info("only hook fd poll timeout time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
        return 0;
    }

    pthread_join(tid, NULL);
    // printf("pthread normal return\n");
    clock_gettime(CLOCK_REALTIME, &finish);
    sub_timespec(start, finish, &delta);
    log_info("only hook fd poll time: %d.%.9ld", (int)delta.tv_sec, delta.tv_nsec);
    return hook_poll_arg.rv;
}