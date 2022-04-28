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

#include "share_queue.h"
#include "socket.h"
#include "share.h"
#include "queue.h"
  
#define BUF_SIZE 100
#define ROUND_TIME 5

// print char array in hex
void my_print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
}

mysocket socket_cli;

ssize_t my_recv(int sockfd, void *buf, size_t len, int flags){
    while(socket_cli.response_queue->current_size == 0);

    if(pthread_mutex_lock(socket_cli.response_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    buffer *b = stream_dequeue(shm_ptr, socket_cli.response_queue, len);
    if(b->buf != NULL){
        memcpy(buf, b->buf, b->length *sizeof(char));
        count = b->length;
        free(b->buf); 
    }
    free(b);
    if(pthread_mutex_unlock(socket_cli.response_lock) != 0) perror("pthread_mutex_unlock failed");

    return count;
}

ssize_t my_send(int sockfd, const void *buf, size_t len, int flags){
    while(socket_cli.request_queue->current_size == socket_cli.request_queue->capacity);

    if(pthread_mutex_lock(socket_cli.request_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    count = stream_enqueue(shm_ptr, socket_cli.request_queue, (char *)buf, len);
    if(pthread_mutex_unlock(socket_cli.request_lock) != 0) perror("pthread_mutex_unlock failed");

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
        printf("socket not initialized\n");
        return -1;
    }

    connection c = (connection){
        .client_fd = sockfd
    };
    memcpy(&c.addr, addr, sizeof(struct sockaddr));

    while(connect_queue_ptr->size == connect_queue_ptr->capacity);
    if(pthread_mutex_lock(connect_lock) != 0) perror("pthread_mutex_lock failed");
    if(Connect_enqueue(connect_shm_ptr, connect_queue_ptr, c) == -1)
        printf("connect queue enqueue failed\n");
    if(pthread_mutex_unlock(connect_lock) != 0) perror("pthread_mutex_unlock failed");
    
    while(accept_queue_ptr->size == 0);
    if(pthread_mutex_lock(accept_lock) != 0) perror("pthread_mutex_lock failed");
    acception *a = Accept_dequeue(connect_shm_ptr, accept_queue_ptr);
    if(pthread_mutex_unlock(accept_lock) != 0) perror("pthread_mutex_unlock failed");
    
    if(a != NULL){
        if(a->client_fd != sockfd){
            printf("fd is different\n");
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
        printf("accept queue dequeue failed\n");
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
        .file_status_flags = 0
    };

    // return original_socket(domain, type, protocol);
    return 999;
}

int my_close(int fd){
    if(socket_cli.in_use != 1){
        printf("socket not in use\n");
        return -1;
    }

    // clear socket_cli
    memset(&socket_cli, 0, sizeof(mysocket));

    return 0;
}

__attribute__((constructor)) void init(){
    // initialize share memory
    shm_fd = shm_open("message_sm", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0){
        perror("shm_open failed");
        exit(1);
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
        exit(1);
    }
    ftruncate(connect_shm_fd, CONNECT_SHM_SIZE);

    connect_shm_ptr = mmap(NULL, CONNECT_SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, connect_shm_fd, 0);
    if(connect_shm_ptr == (void *)-1){
        perror("mmap failed");
    }
    connect_queue_ptr = (connect_queue *)(connect_shm_ptr);
    accept_queue_ptr = (accept_queue *)(connect_shm_ptr+sizeof(connect_queue));
    connect_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue));
    accept_lock = (pthread_mutex_t *)(connect_shm_ptr+sizeof(connect_queue)+sizeof(accept_queue)+sizeof(pthread_mutex_t));

    // initialize socket_cli
    memset(&socket_cli, 0, sizeof(mysocket));
}

int main()  
{  
    struct sockaddr_in server;  
    int sock;  
    char buf[BUF_SIZE];  
    int n;  
    sock = my_socket(AF_INET, SOCK_STREAM, 0);

    server.sin_family = AF_INET;  
    server.sin_port = htons(12345);  

    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr.s_addr);  

    if(my_connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1){
        printf("my_connect failed\n");
        exit(1);
    }

    for(int i = 0; i < ROUND_TIME; i++){
        memset(buf, (i+1)%100, BUF_SIZE);
        if((n = my_send(sock, buf, BUF_SIZE, 0)) < 0){
            printf("send failed\n");
        } 
        printf("[Info] Send %d bytes\n", n);

        int recv_count = 0;
        // while(recv_count < BUF_SIZE){
            if((n = my_recv(sock, buf+recv_count, BUF_SIZE-recv_count, 0)) < 0){
                printf("recv failed\n");
            }
            // recv_count += n;
        // }
        printf("[Info] Receive %d bytes\n", n);
        my_print_hex(buf, n);

        // sleep(0.01);
    }

    if(my_close(sock) == -1)
        printf("close failed\n");

    return 0;  
}  