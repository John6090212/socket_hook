#include <stdio.h>  
#include <string.h>  
#include <unistd.h>  
#include <sys/types.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  
  
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
    struct sockaddr_in server;  
    int sock;  
    char buf[BUF_SIZE];  
    int n;  
    sock = socket(AF_INET, SOCK_STREAM, 0);  

    server.sin_family = AF_INET;  
    server.sin_port = htons(12345);  

    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr.s_addr);  

    if(connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        perror("connect failed");

    //sleep(1);

    for(int i = 0; i < ROUND_TIME; i++){
        memset(buf, (i+1)%100, BUF_SIZE); 

        if((n = send(sock, buf, BUF_SIZE, 0)) < 0){
            perror("send failed\n");
        }             
        
        printf("[Info] Send %d bytes\n", n);
        
        /*
        memset(buf, (i+2)%100, BUF_SIZE); 
        if((n = send(sock, buf, 75, 0)) <= 0){
            printf("send failed\n");
        } 
        */
        int recv_count = 0;
        
        while(recv_count < BUF_SIZE){
            if((n = recv(sock, buf+recv_count, BUF_SIZE-recv_count, 0)) < 0){
                perror("recv failed\n");
            }
            // printf("[Info] Received %d bytes\n", n);
            // my_print_hex(buf, n);
            recv_count += n;
        } 
        //printf("[Info] Receive %d bytes\n", n);  
        
    }

    close(sock);  
    return 0;  
}  