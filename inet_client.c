#include <stdio.h>  
#include <string.h>  
#include <unistd.h>  
#include <stdlib.h>
#include <sys/types.h>  
#include <sys/socket.h>  
#include <netinet/in.h>  
#include <arpa/inet.h>  
#include <poll.h>
  
#define BUF_SIZE 10000
#define ROUND_TIME 4

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
    server.sin_port = htons(5158);  

    inet_pton(AF_INET, "127.0.0.1", &server.sin_addr.s_addr);  

    if(connect(sock, (struct sockaddr *)&server, sizeof(server)) < 0)
        perror("connect failed");

    //sleep(1);
    
    char filename[100] = {0};
    memset(filename, 0, 100);
    for(int i = 0; i < ROUND_TIME; i++){
        memset(buf, (i+1)%100, BUF_SIZE); 


        snprintf(filename, 100, "dcmqrscp_seed/dicom_echo_%d", i+1);
        FILE *file = fopen(filename,"rb");
        if(file == NULL){
            perror("fopen failed");
            exit(-1);
        }
        size_t file_size;
        if((file_size = fread(buf, 1, BUF_SIZE, file)) <= 0){
            printf("read query failed\n");
            exit(-1);
        }
        fclose(file);
        if((n = write(sock, buf, file_size)) < 0){
            perror("send failed\n");
        }             

        printf("[Info] Send %d bytes\n", n);

        int recv_count = 0;
        /*
        struct pollfd pollfds[1];
        pollfds[0].fd = sock;
        //pollfds[0].events = POLLOUT; 
        pollfds[0].events |= POLLIN;
        shutdown(sock, SHUT_RDWR);
        int rv = poll(pollfds, 1, -1);
        printf("rv = %d\n", rv);
        printf("revents = %hd\n", pollfds[0].revents);
        if(pollfds[0].revents & POLLIN){
            printf("rv pollin\n");
        }*/
        //while(recv_count < BUF_SIZE){
            if((n = read(sock, buf+recv_count, BUF_SIZE-recv_count)) < 0){
                perror("recv failed\n");
            }
            // my_print_hex(buf, n);
            //recv_count += n;
        //} 
        printf("[Info] Receive %d bytes\n", n);
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout, sizeof(timeout));
        while(n > 0){
            if((n = read(sock, buf+recv_count, BUF_SIZE-recv_count)) < 0){
                perror("recv failed\n");
                break;
            }
            printf("[Info] Receive %d bytes\n", n);
        }
        printf("[Info] Receive %d bytes\n", n);  
    }
    
    close(sock);  
    return 0;  
}  