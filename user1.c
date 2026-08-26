/*
    Assignment 4 Submission
    Name: Pranav Jha
    Roll number: 22CS30061 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "ksocket.h"

int main(int argc, char **argv)
{
    char filename[100];
    char SRC_IP[20], DEST_IP[20];
    strcpy(SRC_IP, "127.0.0.1");
    strcpy(DEST_IP, "127.0.0.1");
    int SRC_PORT = 6000, DEST_PORT = 6001;

    if(argc == 2)
    {
        strcpy(filename, argv[1]);
    }
    else if(argc == 4)
    {
        strcpy(filename, argv[1]);
        SRC_PORT = atoi(argv[2]);
        DEST_PORT = atoi(argv[3]);
    }
    else if(argc == 6)
    {
        strcpy(filename, argv[1]);
        strcpy(SRC_IP, argv[2]);
        SRC_PORT = atoi(argv[3]);
        strcpy(DEST_IP, argv[4]);
        DEST_PORT = atoi(argv[5]);
    }
    else
    {
        printf("Usage 1: %s <filename> (src and dest port default 6000 and 6001, src and dest ip default 127.0.0.1)\n", argv[0]);
        printf("Usage 2: %s <filename> <src_port> <dest_port> (src and dest ip default 127.0.0.1)\n", argv[0]);
        printf("Usage 3: %s <filename> <src_ip> <src_port> <dest_ip> <dest_port>\n", argv[0]);
        exit(1);
    }

    srand(time(0));
    int sockfd = k_socket(AF_INET, SOCK_KTP, 0);
    if(sockfd == -1)
    {
        k_perror("Error in creating socket");
        exit(1);
    }
    printf("Socket created %d\n", sockfd);

    struct sockaddr_in destAddr, srcAddr;
    destAddr.sin_family = AF_INET;
    destAddr.sin_port = htons(DEST_PORT);
    destAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    srcAddr.sin_family = AF_INET;
    srcAddr.sin_port = htons(SRC_PORT);
    srcAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if(k_bind(sockfd, &destAddr, sizeof(destAddr), &srcAddr, sizeof(srcAddr)) == -1)
    {
        k_perror("Error in binding");
        exit(1);
    }
    printf("Socket bound\n");

    FILE *fp = fopen(filename, "r");
    if(fp == NULL)
    {
        perror("Error in opening file");
        exit(1);
    }
    // read 512 bytes a time and send
    char sendBuf[DATASIZE];
    int len, cnt = 1;
    while((len = fread(sendBuf, 1, DATASIZE, fp)) > 0)
    {
        while(k_sendto(sockfd, sendBuf, len, 0, &destAddr, sizeof(destAddr)) == -1)
        {
            if(globalEno != ENOSPACE)
            {
                k_perror("Error in sending");
                exit(1);
            }
        }
        printf("Sent packet %d\n", cnt++);
        // usleep(rand() % 1000000);
    }

    sprintf(sendBuf, "DONE");
    while(k_sendto(sockfd, sendBuf, strlen(sendBuf) + 1, 0, &destAddr, sizeof(destAddr)) == -1);
    printf("Sent packet DONE\n");

    printf("All packets sent\n"); 
    fclose(fp);

    if(k_close(sockfd) == -1) // blocking call, waits till all the transmission are over
    {
        k_perror("Error in closing");
        exit(1);
    }

    return 0;
    pause();
}
     
