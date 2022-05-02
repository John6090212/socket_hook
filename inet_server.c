
#include <stdio.h>  
#include <unistd.h>  
#include <sys/types.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h> 
#include <sys/time.h> 
#include <string.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <time.h>

// #include "poll.h"

#define BUF_SIZE 100
#define ROUND_TIME 2

// print char array in hex
void my_print_hex(char *m, int length){
    for(int i = 0; i < length; i++){
        printf("%02x", m[i]);
    }
    printf("\n");
}

int main()  
{
    int sock0;  
    struct sockaddr_in addr;  
    struct sockaddr_in client;  
    socklen_t len;  
    int sock_client;  

    sock0 = socket(AF_INET, SOCK_STREAM, 0);  
    if (setsockopt(sock0, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    addr.sin_family = AF_INET;  
    addr.sin_port = htons(12345);  
    addr.sin_addr.s_addr = INADDR_ANY;  
    bind(sock0, (struct sockaddr*)&addr, sizeof(addr));  
    //printf("[Info] binding...\n");  

    listen(sock0, 5);  
    //printf("[Info] listening...\n");

    struct sockaddr_in addrtest;
    socklen_t lentest = sizeof(addrtest);
    // getsockname(sock0, (struct sockaddr *)&addrtest, &lentest);
    // printf("socket name: %s, socket port: %d\n", inet_ntoa(addrtest.sin_addr), addrtest.sin_port);
    //printf("[Info] wait for connection...\n");  
    len = sizeof(client); 
    int flags;
    // if ((flags = fcntl(sock0, F_GETFL)) == -1 || fcntl(sock0, F_SETFL, flags | O_NONBLOCK) == -1)
        // printf("set non-blocking failed\n");

    /*
    int sock1;  
    struct sockaddr_in addr2;    
    sock1 = socket(AF_INET, SOCK_DGRAM, 0);  
    if (setsockopt(sock1, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed");
    addr2.sin_family = AF_INET;  
    addr2.sin_port = htons(12346);  
    addr2.sin_addr.s_addr = INADDR_ANY;  
    bind(sock1, (struct sockaddr*)&addr2, sizeof(addr2)); 
    // test poll
    poll_reset();
    poll_listen(sock0, POLLIN);
    // poll_listen(sock1, POLLIN);

    int rv = do_poll(5000);
    printf("rv = %d\n", rv);
    if(poll_check(sock0, POLLIN)){
        printf("easy\n");
        sock_client = accept(sock0, (struct sockaddr *)&client, &len);  
    }
    else{
        printf("ufds[0] no POLL_IN\n");
        sock_client = accept(sock0, (struct sockaddr *)&client, &len); 
    }
    */
    sock_client = accept(sock0, (struct sockaddr *)&client, &len); 
    // getpeername(sock_client, (struct sockaddr *)&addrtest, &lentest);
    // printf("socket name: %s, socket port: %d\n", inet_ntoa(addrtest.sin_addr), addrtest.sin_port);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    // setsockopt(sock_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE*sizeof(char));
    int msg_count = 0;
    int n = 0;

    for(int i = 0; i < ROUND_TIME; i++){
        // printf("i = %d\n", i);
        int recv_count = 0;
        // while(recv_count < BUF_SIZE){
            if((n = read(sock_client,buf+recv_count,BUF_SIZE-recv_count)) < 0){
                perror("recv failed");
                break;
            }
            // recv_count += n;
        // }
        
        msg_count++;
        // printf("[Info] Received %d message\n", msg_count);
        printf("[Info] Received %d bytes\n", n);
        my_print_hex(buf, n);

        memset(buf, (i+11)%100, BUF_SIZE);
        if ((n = write(sock_client,buf,BUF_SIZE)) < 0){
            printf("server send failed\n");
        }

        printf("[Info] Send %d bytes\n", n);
    }
    //printf("[Info] Close client connection...\n");  


    close(sock_client);  

    //printf("[Info] Close self connection...\n");  
    close(sock0);  
    
    return 0;  
}  