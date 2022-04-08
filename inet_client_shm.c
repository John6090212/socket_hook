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
#include "share.h" 
  
#define BUF_SIZE 100
#define ROUND_TIME 1

// print char array in hex
void my_print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
}

ssize_t my_recv(int sockfd, void *buf, size_t len, int flags){
    while(response_queue->current_size == 0);

    if(pthread_mutex_lock(response_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    buffer *b = share_dequeue(shm_ptr, response_queue, len);
    if(b->buf != NULL){
        memcpy(buf+count, b->buf, b->length *sizeof(char));
        count = b->length;
        free(b->buf); 
    }
    free(b);
    if(pthread_mutex_unlock(response_lock) != 0) perror("pthread_mutex_unlock failed");

    return count;
}

ssize_t my_send(int sockfd, const void *buf, size_t len, int flags){
    while(request_queue->current_size == request_queue->capacity);

    if(pthread_mutex_lock(request_lock) != 0) perror("pthread_mutex_lock failed");
    ssize_t count = 0;
    count = share_enqueue(shm_ptr, request_queue, (char *)buf, len);
    if(pthread_mutex_unlock(request_lock) != 0) perror("pthread_mutex_unlock failed");

    return count;
}

__attribute__((constructor)) void init(){
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

    request_queue = (share_queue *)shm_ptr;
    response_queue = (share_queue *)(shm_ptr+sizeof(share_queue));
    request_lock = (pthread_mutex_t *)(shm_ptr+2*sizeof(share_queue));
    response_lock = (pthread_mutex_t *)(shm_ptr+2*sizeof(share_queue)+sizeof(pthread_mutex_t));
}

int main()  
{  
    struct sockaddr_in server;  
    int sock;  
    char buf[BUF_SIZE];  
    int n;  
    sock = socket(AF_INET, SOCK_STREAM, 0);  

    server.sin_family = AF_INET;  
    server.sin_port = htons(12345);  

    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr.s_addr);  

    connect(sock, (struct sockaddr *)&server, sizeof(server));  

    for(int i = 0; i < ROUND_TIME; i++){
        memset(buf, i%100, BUF_SIZE); 
        if((n = my_send(sock, buf, BUF_SIZE, 0)) <= 0){
            printf("send failed\n");
        } 
        // printf("[Info] Send %d bytes\n", n);  

        int recv_count = 0;
        while(recv_count < BUF_SIZE){
            if((n = my_recv(sock, buf+recv_count, BUF_SIZE-recv_count, 0)) <= 0){
                printf("recv failed\n");
            }
            recv_count += n;
        }            
        // printf("[Info] Receive %d bytes\n", n);  
        // my_print_hex(buf, n);

        sleep(0.01);
    }

    close(sock);  
    return 0;  
}  