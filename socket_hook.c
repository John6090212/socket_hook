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
#include "share_queue.h"
#include "queue.h"
#include "socket.h"
#include "share.h"
#include "stack.h"

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
int (*original_ioctl)(int fd, unsigned long request, ...);
ssize_t (*original_recvfrom)(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);
ssize_t (*original_sendto)(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t (*original_recvmsg)(int socket, struct msghdr *message, int flags);
ssize_t (*original_sendmsg)(int socket, const struct msghdr *message, int flags);

// queue to acquire available fd
Stack *available_fd;
// queue to acquire available share_unit
Queue *available_share_unit;
// save information of self-management socket
mysocket socket_arr[1024];

// for debug
void my_print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
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
    if(pthread_mutex_init(request_lock, &attr) != 0) perror("pthread_mutex_init");

    pthread_mutex_t *response_lock = (pthread_mutex_t *)(shm_ptr+i*sizeof(share_unit)+2*sizeof(share_queue)+sizeof(pthread_mutex_t));
    if(pthread_mutex_init(response_lock, &attr) != 0) perror("pthread_mutex_init");
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
        .file_status_flags = 0
    };
    socket_arr[fd].share_unit_index = dequeue(available_share_unit);
    if(socket_arr[fd].share_unit_index == INT_MIN){
        printf("need to increase SOCK_NUM\n");
        return -1;
    }
    init_share_queue(socket_arr[fd].share_unit_index);
    socket_arr[fd].request_queue = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].request_queue);
    socket_arr[fd].response_queue = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].response_queue);
    socket_arr[fd].request_lock = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].request_lock);
    socket_arr[fd].response_lock = &(((share_unit *)shm_ptr)[socket_arr[fd].share_unit_index].response_lock);
    return 0;
}

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
    original_read = dlsym(RTLD_NEXT, "read");
    original_write = dlsym(RTLD_NEXT, "write");
    original_setsockopt = dlsym(RTLD_NEXT, "setsockopt");
    original_getsockopt = dlsym(RTLD_NEXT, "getsockopt");
    original_fcntl = dlsym(RTLD_NEXT, "fcntl");
    original_ioctl = dlsym(RTLD_NEXT, "ioctl");
    original_recvfrom = dlsym(RTLD_NEXT, "recvfrom");
    original_sendto = dlsym(RTLD_NEXT, "sendto");
    original_recvmsg = dlsym(RTLD_NEXT, "recvmsg");
    original_sendmsg = dlsym(RTLD_NEXT, "sendmsg");

    // initialize available fd queue
    available_fd = createStack(1024);
    for(int i = 0; i <=1023; i++){
        push(available_fd, i);
    }

    // initialize available share unit queue
    available_share_unit = createQueue(SOCKET_NUM);
    for(int i = 0; i < SOCKET_NUM; i++){
        enqueue(available_share_unit, i);
    }

    // initialize socket array
    memset(socket_arr, 0, 1024*sizeof(mysocket));

    // initialize random seed
    srand(time(NULL));

    // initialize communication share memory
    shm_fd = shm_open("message_sm", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        perror("shm_open failed");
        exit(999);
    }
    ftruncate(shm_fd, COMMUNICATE_SHM_SIZE);

    shm_ptr = mmap(NULL, COMMUNICATE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == (void *)-1){
        perror("mmap failed");
    }

    // initialize connect share memory
    connect_shm_fd = shm_open("connect_sm", O_CREAT | O_RDWR, 0666);
    if (connect_shm_fd < 0){
        perror("shm_open failed");
        exit(999);
    }
    ftruncate(connect_shm_fd, CONNECT_SHM_SIZE);

    connect_shm_ptr = mmap(NULL, CONNECT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, connect_shm_fd, 0);
    if(connect_shm_ptr == (void *)-1){
        perror("mmap failed");
    }

    // initialize mutex lock for connect share memory
    //initialize mutex attr
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    //lock owner dies without unlocking it, any future attempts to acquire lock on this mutex will succeed
    pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    connect_lock = (pthread_mutex_t *)(connect_shm_ptr);
    if(pthread_mutex_init(connect_lock, &attr) != 0) perror("pthread_mutex_init");
    connect_sa_ptr = (struct sockaddr *)(connect_shm_ptr+sizeof(pthread_mutex_t));

    // initialize request/response queue
    for(int i = 0; i < SOCKET_NUM; i++){
        init_share_queue(i);
    }
}

int is_valid_fd(int sockfd){
    if(sockfd >= 0 && sockfd <= 1023 && socket_arr[sockfd].in_use == 1)
        return 1;
    else
        return 0;
}

int socket(int domain, int type, int protocol){
    // printf("hook socket()!\n");
    // filter out non-tcp and unix socket for dnsmasq
    if(domain == AF_UNIX || type != SOCK_STREAM)
        return original_socket(domain, type, protocol);

    int fd = pop(available_fd);
    if(fd == INT_MIN){
        errno = EMFILE;
        return -1;
    }
    if (init_socket(fd, domain, type, protocol) == -1)
        return -1;

    return fd;
}

int bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    // printf("hook bind()!\n");
    if(is_valid_fd(sockfd)){  
        // check addrlen is valid      
        if(addrlen > sizeof(struct sockaddr)){
            errno = ENAMETOOLONG;
            return -1;
        }
        socket_arr[sockfd].has_bind = 1;
        memcpy(&socket_arr[sockfd].addr, addr, addrlen);
    }
    else
        return original_bind(sockfd, addr, addrlen);

    return 0;
}

int listen(int sockfd, int backlog){
    // printf("hook listen()!\n");
    if(!is_valid_fd(sockfd)){
        return original_listen(sockfd, backlog);
    }

    return 0;
}

int accept(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    printf("hook accept()!\n");
    int fd = 0;
    if(is_valid_fd(sockfd)){
        fd = pop(available_fd);
        if(fd == INT_MIN){
            errno = EMFILE;
            return -1;
        }
        if(init_socket(fd, socket_arr[sockfd].domain, socket_arr[sockfd].type, socket_arr[sockfd].protocol) == -1)
            return -1;

        // save bind address to connect share memory
        if(socket_arr[sockfd].has_bind == 1 && socket_arr[fd].share_unit_index >= 0 && socket_arr[fd].share_unit_index < SOCKET_NUM){
            if(pthread_mutex_lock(connect_lock) != 0) perror("pthread_mutex_lock failed");
            memcpy(&connect_sa_ptr[socket_arr[fd].share_unit_index], &socket_arr[sockfd].addr, sizeof(struct sockaddr));
            if(pthread_mutex_unlock(connect_lock) != 0) perror("pthread_mutex_unlock failed");
        }
        else{
            printf("not bind or share unit index out of bound!\n");
        }

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
    else
        return original_accept(sockfd, addr, addrlen);

    return fd;
}

int connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    // printf("hook connect()!\n");
    if(is_valid_fd(sockfd)){
        memcpy(&socket_arr[sockfd].peer_addr, addr, sizeof(struct sockaddr));
    }
    else
        return original_connect(sockfd, addr, addrlen);

    return 0;
}

int close(int fd){
    // printf("hook close()!\n");
    if(is_valid_fd(fd)){
        enqueue(available_share_unit, socket_arr[fd].share_unit_index);
        // clear address in connect sockaddr array
        if(pthread_mutex_lock(connect_lock) != 0) perror("pthread_mutex_lock failed");
        memset(&connect_sa_ptr[socket_arr[fd].share_unit_index], 0, sizeof(struct sockaddr));
        if(pthread_mutex_unlock(connect_lock) != 0) perror("pthread_mutex_unlock failed");
        memset(&socket_arr[fd], 0, sizeof(mysocket));
        push(available_fd, fd);
    }
    else
        return original_close(fd);

    return 0;
}

int shutdown(int sockfd, int how){
    // printf("hook shutdown()!\n");
    if(is_valid_fd(sockfd)){
        if(how == SHUT_RD)
            socket_arr[sockfd].shutdown_read = 1;
        else if(how == SHUT_WR)
            socket_arr[sockfd].shutdown_write = 1;
        else if(how == SHUT_RDWR){
            socket_arr[sockfd].shutdown_read = 1;
            socket_arr[sockfd].shutdown_write = 1;
        }
    }
    else
        return original_shutdown(sockfd, how);

    return 0;
}

int getsockname(int sockfd, struct sockaddr *restrict addr, socklen_t *restrict addrlen){
    // printf("hook getsockname()!\n");
    if(is_valid_fd(sockfd)){
        memcpy(addr, &socket_arr[sockfd].addr, sizeof(struct sockaddr));
    }
    else
        return original_getsockname(sockfd, addr, addrlen);

    return 0;
}

ssize_t recv(int sockfd, void *buf, size_t len, int flags){
    if(!is_valid_fd(sockfd))
        return original_recv(sockfd, buf, len, flags);

    // deal with shutdown
    if(socket_arr[sockfd].shutdown_read)
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
        while(socket_arr[sockfd].request_queue->current_size == 0);

        if(pthread_mutex_lock(socket_arr[sockfd].request_lock) != 0) perror("pthread_mutex_lock failed");
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
            printf("not implement socket type in recv!");
            exit(999);
        }
        if(pthread_mutex_unlock(socket_arr[sockfd].request_lock) != 0) perror("pthread_mutex_unlock failed");
        // skip if no MSG_WAITALL or socket type is datagram (MSG_WAITALL has no effect)
        if(!(flags & MSG_WAITALL) | socket_arr[sockfd].type == SOCK_DGRAM) break;
    }

    return count;
}

ssize_t send(int sockfd, const void *buf, size_t len, int flags){
    if(!is_valid_fd(sockfd))
        return original_send(sockfd, buf, len, flags);

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
    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity);

    ssize_t count = 0;
    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) perror("pthread_mutex_lock failed");
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
                    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity);

                    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) perror("pthread_mutex_lock failed");
                    if(i == send_times - 1) 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf+i*LO_MSS, total_len-i*LO_MSS);                   
                    else 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, temp_buf+i*LO_MSS, LO_MSS);
                    
                    if(temp_count > 0)
                        count += temp_count;
                }                

                if(i != send_times - 1){
                    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) perror("pthread_mutex_unlock failed");
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
                    while(socket_arr[sockfd].response_queue->current_size == socket_arr[sockfd].response_queue->capacity);

                    if(pthread_mutex_lock(socket_arr[sockfd].response_lock) != 0) perror("pthread_mutex_lock failed");
                    if(i == send_times - 1) 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf+i*LO_MSS, len-i*LO_MSS);                   
                    else 
                        temp_count = stream_enqueue(shm_ptr, socket_arr[sockfd].response_queue, (char *)buf+i*LO_MSS, LO_MSS);
                    
                    if(temp_count > 0)
                        count += temp_count;
                }                

                if(i != send_times - 1){
                    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) perror("pthread_mutex_unlock failed");
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
        printf("not implement socket type in send!");
        exit(999);
    }
    if(pthread_mutex_unlock(socket_arr[sockfd].response_lock) != 0) perror("pthread_mutex_unlock failed");
    
    return count;
}

int socketpair(int domain, int type, int protocol, int sv[2]){
    // printf("hook socketpair()!\n");
    // filter out non-tcp and unix socket (dnsmasq not use this function)
    if(domain == AF_UNIX || type != SOCK_STREAM)
        return original_socketpair(domain, type, protocol, sv);

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
    // printf("hook getpeername()!\n");
    if(is_valid_fd(sockfd)){
        memcpy(addr, &socket_arr[sockfd].peer_addr, sizeof(struct sockaddr));
    }
    else
        return original_getpeername(sockfd, addr, addrlen);

    return 0;
}

ssize_t read(int fd, void *buf, size_t count){
    if(!is_valid_fd(fd))
        return original_read(fd, buf, count);

    // deal with shutdown
    if(socket_arr[fd].shutdown_read)
        return 0;

    // handle nonblocking flag
    if(socket_arr[fd].file_status_flags & O_NONBLOCK){
        if(socket_arr[fd].request_queue->current_size == 0){
            errno = EWOULDBLOCK;
            return -1;
        }
    }
    while(socket_arr[fd].request_queue->current_size == 0);

    if(pthread_mutex_lock(socket_arr[fd].request_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t my_count = 0;
    buffer *b = stream_dequeue(shm_ptr, socket_arr[fd].request_queue, count);
    if(b->buf != NULL){
        memcpy(buf, b->buf, b->length *sizeof(char));
        my_count = b->length;
        free(b->buf);
    }
    free(b);
    if(pthread_mutex_unlock(socket_arr[fd].request_lock) != 0) perror("pthread_mutex_unlock failed");

    return my_count;
}

ssize_t write(int fd, const void *buf, size_t count){
    if(!is_valid_fd(fd))
        return original_write(fd, buf, count);

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
    while(socket_arr[fd].response_queue->current_size == socket_arr[fd].response_queue->capacity);

    if(pthread_mutex_lock(socket_arr[fd].response_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t my_count = 0;
    my_count = stream_enqueue(shm_ptr, socket_arr[fd].response_queue, (char *)buf, count);
    if(pthread_mutex_unlock(socket_arr[fd].response_lock) != 0) perror("pthread_mutex_unlock failed");

    return my_count;
}

int setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen){
    if(!is_valid_fd(sockfd))
        return original_setsockopt(sockfd, level, optname, optval, optlen);

    return 0;
}

int getsockopt(int sockfd, int level, int optname, void *restrict optval, socklen_t *restrict optlen){
    if(!is_valid_fd(sockfd))
        return original_getsockopt(sockfd, level, optname, optval, optlen);

    return 0;
}

int fcntl(int fd, int cmd, ...){
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
                printf("fcntl cmd not implement yet\n");
                exit(999);
        }
    }
    else{
        switch(cmd){
            case F_GETFL:
                va_end(ap);
                return socket_arr[fd].file_status_flags;
            case F_SETFL:
                flags = va_arg(ap, int);
                va_end(ap);
                socket_arr[fd].file_status_flags = flags;
                return 0;
            default:
                va_end(ap);
                printf("fcntl cmd not implement yet\n");
                exit(999);
        }
    }
}

int ioctl(int fd, unsigned long request, ...){
    va_list ap;
    va_start(ap, request);
    int flags;
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
    else{
        va_end(ap);
        return 0;
    }
}

ssize_t recvfrom(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len){
    if(!is_valid_fd(socket))
        return original_recvfrom(socket, buffer, length, flags, address, address_len);
    else{
        printf("recvfrom not implement this part\n");
        exit(999);
    }
}

ssize_t sendto(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len){
    if(!is_valid_fd(socket))
        return original_sendto(socket, message, length, flags, dest_addr, dest_len);
    else{
        printf("sendto not implement this part\n");
        exit(999);
    }
}

ssize_t recvmsg(int socket, struct msghdr *message, int flags){
    if(!is_valid_fd(socket))
        return original_recvmsg(socket, message, flags);
    else{
        printf("recvmsg not implement this part\n");
        exit(999);
    }
}

ssize_t sendmsg(int socket, const struct msghdr *message, int flags){
    if(!is_valid_fd(socket))
        return original_sendmsg(socket, message, flags);
    else{
        printf("sendmsg not implement this part\n");
        exit(999);
    }
}