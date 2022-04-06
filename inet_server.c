
#include <stdio.h>  
#include <unistd.h>  
#include <sys/types.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h> 
#include <sys/time.h> 

#define BUF_SIZE 100
#define ROUND_TIME 100000000

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

    addr.sin_family = AF_INET;  
    addr.sin_port = htons(12345);  
    addr.sin_addr.s_addr = INADDR_ANY;  
    bind(sock0, (struct sockaddr*)&addr, sizeof(addr));  
    //printf("[Info] binding...\n");  

    listen(sock0, 5);  
    //printf("[Info] listening...\n");  

    //printf("[Info] wait for connection...\n");  
    len = sizeof(client);  
    sock_client = accept(sock0, (struct sockaddr *)&client, &len);  

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;
    // setsockopt(sock_client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    char buf[BUF_SIZE];
    int msg_count = 0;
    int n = 0;
    for(int i = 0; i < ROUND_TIME; i++){
        printf("i = %d\n", i);
        int recv_count = 0;
        while(recv_count < BUF_SIZE){
            if((n = recv(sock_client,buf+recv_count,BUF_SIZE-recv_count,0)) < 0){
                printf("recv failed\n");
                break;
            }
            recv_count += n;
        }
        
        msg_count++;
        // printf("[Info] Received %d message\n", msg_count);
        // printf("[Info] Received %d bytes\n", n);
        // my_print_hex(buf, n);
        if ((n = send(sock_client,buf,BUF_SIZE,0)) <= 0){
            printf("server send failed\n");
        }
        // printf("[Info] Send %d bytes\n", n);
    }
    
    //printf("[Info] Close client connection...\n");  
    close(sock_client);  

    //printf("[Info] Close self connection...\n");  
    close(sock0);  
    return 0;  
}  