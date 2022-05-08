#define _GNU_SOURCE
#include <sys/mman.h>
#include <fcntl.h> // O_* constant
#include <sys/stat.h> // mode constants
#include <stdlib.h>
#include <stdio.h>  
#include <string.h>  
#include <unistd.h>  
#include <sys/types.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h> 
#include <errno.h>
// for timer
#include <signal.h>
#include <time.h>
#include <sys/timerfd.h>
#include <stdint.h>
// for poll
#include <poll.h>

#include "share_queue.h"
#include "socket.h"
#include "share.h"
#include "queue.h"
#include "log.h"
  
#define BUF_SIZE 100
#define ROUND_TIME 2

// print char array in hex
void my_print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
}

mysocket socket_cli;

void my_signal_handler(int signum){
    if(signum == SIGUSR2){
        __sync_val_compare_and_swap(&socket_cli.is_socket_timeout, 0, 1);
    }
}

int my_createtimer(timer_t *timer){
    struct sigevent evp = (struct sigevent){
        .sigev_value.sival_ptr = timer,
        .sigev_notify = SIGEV_SIGNAL,
        .sigev_signo = SIGUSR2
    };

    return timer_create(CLOCK_REALTIME, &evp, timer);
}

int my_settimer(int is_send){
    timer_t timer;
    struct timeval tv;
    if(is_send){
        timer = socket_cli.send_timer;
        tv = socket_cli.send_timeout;
    }
    else{
        timer = socket_cli.recv_timer;
        tv = socket_cli.recv_timeout;
    }

    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = tv.tv_sec + (time_t)(tv.tv_usec / 1000000),
            .tv_nsec = (long)(tv.tv_usec % 1000000) * 1000
        }
    };
    if(timer_settime(timer, 0, &new_value, NULL) == -1){
        log_error("settimer failed, %s", strerror(errno));
        return -1;
    }

    return 0;
}

int my_stoptimer(int is_send){
    timer_t timer;
    if(is_send)
        timer = socket_cli.send_timer;
    else
        timer = socket_cli.recv_timer;

    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        }
    };    
    if(timer_settime(timer, TIMER_ABSTIME, &new_value, NULL) == -1){
        log_error("stoptimer failed");
        return -1;
    }

    return 0;
}

ssize_t my_recv(int sockfd, void *buf, size_t len, int flags){
    if(socket_cli.in_use != 1){
        log_error("socket not in use\n");
        return -1;
    }

    if(close_arr[socket_cli.share_unit_index].server_write)
        return 0;

    bool need_timeout = false;

    while(__sync_bool_compare_and_swap(&socket_cli.response_queue->current_size, 0, 0)){
        if(close_arr[socket_cli.share_unit_index].server_write){
            if(need_timeout && my_stoptimer(false) == -1)
                return -1;    

            return 0;    
        }

        if(!need_timeout && (socket_cli.recv_timeout.tv_sec > 0 || socket_cli.recv_timeout.tv_usec > 0)){
            if(socket_cli.recv_timer == NULL){
                if(my_createtimer(&socket_cli.recv_timer) == -1){
                    log_error("recv_timer create failed");
                    socket_cli.recv_timer = NULL;
                    return 0;
                }
            }

            socket_cli.is_socket_timeout = 0;
            if(my_settimer(false) == -1)
                return 0;

            need_timeout = true;
        }

        if(socket_cli.recv_timer == NULL)
            continue;

        if(__sync_bool_compare_and_swap(&socket_cli.is_socket_timeout, 1, 1)){
            socket_cli.is_socket_timeout = 0;
            errno = EWOULDBLOCK;
            return -1;
        }
    }

    if(socket_cli.recv_timer != NULL && need_timeout){
        if(my_stoptimer(false) == -1){
            return -1;
        }
    }

    if(pthread_mutex_lock(socket_cli.response_lock) != 0) log_error("pthread_mutex_lock response_lock failed");
    ssize_t count = 0;
    buffer *b = stream_dequeue(shm_ptr, socket_cli.response_queue, len);
    if(b->buf != NULL){
        memcpy(buf, b->buf, b->length *sizeof(char));
        count = b->length;
        free(b->buf); 
    }
    free(b);
    if(pthread_mutex_unlock(socket_cli.response_lock) != 0) log_error("pthread_mutex_unlock response_lock failed");

    return count;
}

ssize_t my_send(int sockfd, const void *buf, size_t len, int flags){
    if(socket_cli.in_use != 1){
        log_error("socket not in use");
        return -1;
    }
    
    bool need_timeout = false;
    if(__sync_bool_compare_and_swap(&socket_cli.request_queue->current_size, socket_cli.request_queue->capacity, socket_cli.request_queue->capacity))
        need_timeout = true;

    if(need_timeout && (socket_cli.send_timeout.tv_sec > 0 || socket_cli.send_timeout.tv_usec > 0)){
        if(socket_cli.send_timer == NULL){
            if(my_createtimer(&socket_cli.send_timer) == -1){
                log_error("send_timer create failed");
                return 0;
            }
        }

        socket_cli.is_socket_timeout = 0;

        if(my_settimer(true) == -1)
            return 0;
    }
    
    while(socket_cli.request_queue->current_size == socket_cli.request_queue->capacity){
        if(close_arr[socket_cli.share_unit_index].server_read){
            if(need_timeout && my_stoptimer(true) == -1)
                return -1; 

            return 0;
        }
        
        if(socket_cli.send_timer == NULL)
            continue;

        if(__sync_bool_compare_and_swap(&socket_cli.is_socket_timeout, 1, 1)){
            socket_cli.is_socket_timeout = 0;
            errno = EWOULDBLOCK;
            return -1;
        }        
    }
    
    if(socket_cli.send_timer != NULL && need_timeout){
        if(my_stoptimer(true) == -1){
            return -1;
        }
    }
    
    if(pthread_mutex_lock(socket_cli.request_lock) != 0) log_error("pthread_mutex_lock request_lock failed");
    ssize_t count = 0;
    count = stream_enqueue(shm_ptr, socket_cli.request_queue, (char *)buf, len);
    if(pthread_mutex_unlock(socket_cli.request_lock) != 0) log_error("pthread_mutex_unlock request_lock failed");

    return count;
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

int my_connect(int sockfd, const struct sockaddr *addr, socklen_t addrlen){
    if(socket_cli.in_use != 1){
        log_error("socket not in use");
        return -1;
    }

    connection c = (connection){
        .client_fd = sockfd
    };
    memcpy(&c.addr, addr, sizeof(struct sockaddr));

    while(connect_queue_ptr->size == connect_queue_ptr->capacity);
    if(pthread_mutex_lock(connect_lock) != 0) log_error("pthread_mutex_lock connect_lock failed");
    if(Connect_enqueue(connect_shm_ptr, connect_queue_ptr, c) == -1)
        log_error("connect queue enqueue failed");
    if(pthread_mutex_unlock(connect_lock) != 0) log_error("pthread_mutex_unlock connect_lock failed");
    
    while(accept_queue_ptr->size == 0);
    if(pthread_mutex_lock(accept_lock) != 0) log_error("pthread_mutex_lock accept_lock failed");
    acception *a = Accept_dequeue(connect_shm_ptr, accept_queue_ptr);
    if(pthread_mutex_unlock(accept_lock) != 0) log_error("pthread_mutex_unlock accept_lock failed");
    
    if(a != NULL){
        if(a->client_fd != sockfd){
            log_error("fd is different");
            free(a);
            return -1;
        }
        socket_cli.share_unit_index = a->share_unit_index;
        socket_cli.request_queue = &(((share_unit *)shm_ptr)[socket_cli.share_unit_index].request_queue);
        socket_cli.response_queue = &(((share_unit *)shm_ptr)[socket_cli.share_unit_index].response_queue);
        socket_cli.request_lock = &(((share_unit *)shm_ptr)[socket_cli.share_unit_index].request_lock);
        socket_cli.response_lock = &(((share_unit *)shm_ptr)[socket_cli.share_unit_index].response_lock);
        free(a);
        return 0;
    }
    else{
        log_error("accept queue dequeue failed");
        return -1;
    }
}

int my_socket(int domain, int type, int protocol){
    socket_cli = (mysocket){
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
        .send_timeout = (struct timeval) {
            .tv_sec = 0,
            .tv_usec = 0
        },
        .recv_timeout = (struct timeval) {
            .tv_sec = 0,
            .tv_usec = 0
        },
        .send_timer = NULL,
        .recv_timer = NULL,
        .is_socket_timeout = 0,
        .poll_timer = NULL
    };

    // return original_socket(domain, type, protocol);
    return 999;
}

int my_close(int fd){
    if(socket_cli.in_use != 1){
        log_error("socket not in use");
        return -1;
    }
    if(socket_cli.share_unit_index >= 0){
        close_arr[socket_cli.share_unit_index].client_read = 1;
        close_arr[socket_cli.share_unit_index].client_write = 1;
    }
    // close timer
    if(socket_cli.send_timer != NULL){
        timer_delete(socket_cli.send_timer);
        socket_cli.send_timer = NULL;
    }
    if(socket_cli.recv_timer != NULL){
        timer_delete(socket_cli.recv_timer);
        socket_cli.send_timer = NULL;
    }
    if(socket_cli.poll_timer != NULL){
        timer_delete(socket_cli.poll_timer);
        socket_cli.poll_timer = NULL;
    }

    // clear socket_cli
    memset(&socket_cli, 0, sizeof(mysocket));

    return 0;
}

int my_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen){
    if(socket_cli.in_use != 1){
        log_error("socket not in use");
        return -1;
    }
    
    if(optname == SO_SNDTIMEO){
        struct timeval *tv = (struct timeval *)optval;
        socket_cli.send_timeout.tv_sec = tv->tv_sec;
        socket_cli.send_timeout.tv_usec = tv->tv_usec;
    }

    if(optname == SO_RCVTIMEO){
        struct timeval *tv = (struct timeval *)optval;
        socket_cli.recv_timeout.tv_sec = tv->tv_sec;
        socket_cli.recv_timeout.tv_usec = tv->tv_usec;        
    }

    return 0;
}

int my_poll_settimer(int timeout){
    timer_t timer = socket_cli.poll_timer;

    struct timespec now;
    if (clock_gettime(CLOCK_REALTIME, &now) == -1){
        log_error("clock_gettime failed");
        return -1;
    }

    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = now.tv_sec + (time_t)(timeout / 1000),
            .tv_nsec = now.tv_nsec + (long)(timeout % 1000) * 1000000
        }
    };

    if(timer_settime(timer, TIMER_ABSTIME, &new_value, NULL) == -1){
        log_error("poll settimer failed");
        return -1;
    }

    return 0;
}

int my_poll_stoptimer(void){
    timer_t timer = socket_cli.poll_timer;

    struct itimerspec new_value = (struct itimerspec){
        .it_interval = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        },
        .it_value = (struct timespec){
            .tv_sec = 0,
            .tv_nsec = 0
        }
    };    
    if(timer_settime(timer, TIMER_ABSTIME, &new_value, NULL) == -1){
        log_error("poll stoptimer failed");
        return -1;
    }

    return 0;
}

int my_poll(struct pollfd *fds, nfds_t nfds, int timeout){
    if(socket_cli.in_use != 1){
        log_error("socket not in use");
        return -1;
    }

    int rv = 0;
    fds[0].revents = 0;

    if(timeout == -1){
        while(1){
            if(fds[0].events & POLLIN && socket_cli.response_queue->current_size > 0){
                fds[0].revents |= POLLIN;
                rv++;
                return rv;
            }   

            if(fds[0].events & POLLOUT && socket_cli.request_queue->current_size < socket_cli.request_queue->capacity){
                fds[0].revents |= POLLOUT;
                rv++;
                return rv;
            } 
        }
    }


    if(fds[0].events & POLLIN && socket_cli.response_queue->current_size > 0){
        fds[0].revents |= POLLIN;
        rv++;
    }

    if(fds[0].events & POLLOUT && socket_cli.request_queue->current_size < socket_cli.request_queue->capacity){
        fds[0].revents |= POLLOUT;
        rv++;
    }

    if(timeout == 0 || rv > 0)
        return rv;
    
    if(timeout > 0){
        if(socket_cli.poll_timer == NULL && my_createtimer(&socket_cli.poll_timer) == -1){
            log_error("poll_timer create failed");
            return -1;
        }
        
        socket_cli.is_socket_timeout = 0;

        if(my_poll_settimer(timeout) == -1)
            return -1;
    }
    
    while(1){
        if(fds[0].events & POLLIN && socket_cli.response_queue->current_size > 0){
            fds[0].revents |= POLLIN;
            rv++;
        }

        if(fds[0].events & POLLOUT && socket_cli.request_queue->current_size < socket_cli.request_queue->capacity){
            fds[0].revents |= POLLOUT;
            rv++;
        }

        if(rv > 0){
            if(my_poll_stoptimer() == -1)
                return -1;

            return rv;
        }

        if(__sync_bool_compare_and_swap(&socket_cli.is_socket_timeout, 1, 1)){
            socket_cli.is_socket_timeout = 0;
            return 0;
        }
    }

    return rv;
}


__attribute__((constructor)) void init(){
    // initialize share memory
    shm_fd = shm_open("message_sm", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        log_error("shm_open failed");
        exit(1);
    }
    ftruncate(shm_fd, COMMUNICATE_SHM_SIZE);

    shm_ptr = mmap(NULL, COMMUNICATE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if(shm_ptr == (void *)-1){
        log_error("mmap failed");
    }

    // initialize connect share memory
    connect_shm_fd = shm_open("connect_sm", O_CREAT | O_RDWR, 0666);
    if (connect_shm_fd < 0){
        log_error("shm_open failed");
        exit(1);
    }
    ftruncate(connect_shm_fd, CONNECT_SHM_SIZE);

    connect_shm_ptr = mmap(NULL, CONNECT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, connect_shm_fd, 0);
    if(connect_shm_ptr == (void *)-1){
        log_error("mmap failed");
    }
    connect_queue_ptr = (connect_queue *)(connect_shm_ptr);
    accept_queue_ptr = (accept_queue *)(connect_shm_ptr+sizeof(connect_queue));
    connect_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue));
    accept_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue)+sizeof(pthread_mutex_t));

    // initialize socket_cli
    memset(&socket_cli, 0, sizeof(mysocket));

    // initialize close share memory
    close_shm_fd = shm_open("close_sm", O_CREAT | O_RDWR, 0666);
    if (close_shm_fd < 0){
        log_error("shm_open failed");
        exit(999);
    }
    ftruncate(close_shm_fd, CLOSE_SHM_SIZE);

    close_shm_ptr = mmap(NULL, CLOSE_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, close_shm_fd, 0);
    if(close_shm_ptr == (void *)-1){
        log_error("mmap failed");
    }
    close_arr = (close_unit *)close_shm_ptr;

    // set signal handler for recv and send timeout
    signal(SIGUSR2, my_signal_handler);
}

int main()  
{  
    struct sockaddr_in server;  
    int sock;  
    char buf[BUF_SIZE]; 
    memset(buf, 0, BUF_SIZE); 
    int n;  
    sock = my_socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;  
    server.sin_port = htons(12345);  

    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr.s_addr);  
    struct timeval timeout;
    timeout.tv_sec = 5;
    timeout.tv_usec = 0;
    my_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
    my_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout));
    if(my_connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1){
        printf("my_connect failed\n");
        exit(1);
    }

    for(int i = 0; i < ROUND_TIME; i++){
        memset(buf, (i+1)%100, BUF_SIZE);
        /*
        FILE *file = fopen("test_query","rb");
        if(fread(buf, 1, BUF_SIZE, file) <= 0){
            printf("read query failed\n");
            exit(-1);
        }
        fclose(file);*/
        if((n = my_send(sock, buf, BUF_SIZE, 0)) < 0){
            printf("send failed\n");
        } 
        // printf("[Info] Send %d bytes\n", n);

        int recv_count = 0;
        while(recv_count < BUF_SIZE){
            if((n = my_recv(sock, buf+recv_count, BUF_SIZE-recv_count, 0)) < 0){
                printf("recv failed\n");
            }
            recv_count += n;
        }
        // printf("[Info] Receive %d bytes\n", n);
        // my_print_hex(buf, n);

        // sleep(0.01);
    }
    
    if(my_close(sock) == -1)
        printf("close failed\n");
    
    return 0;  
}