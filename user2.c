/*
    Assignment 4 Submission
    Name: Pranav Jha
    Roll number: 22CS30061 
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include "ksocket.h"

int main(int argc, char **argv)
{
    char filename[100];
    char SRC_IP[20], DEST_IP[20];
    strcpy(SRC_IP, "127.0.0.1");
    strcpy(DEST_IP, "127.0.0.1");
    int SRC_PORT = 6001, DEST_PORT = 6000;

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
        printf("Usage 1: %s <filename to recv in> (src and dest port default 6001 and 6000, src and dest ip default 127.0.0.1)\n", argv[0]);
        printf("Usage 2: %s <filename to recv in> <src_port> <dest_port> (src and dest ip default 127.0.0.1)\n", argv[0]);
        printf("Usage 3: %s <filename to recv in> <src_ip> <src_port> <dest_ip> <dest_port>\n", argv[0]);
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
    destAddr.sin_addr.s_addr = inet_addr(DEST_IP);

    srcAddr.sin_family = AF_INET;
    srcAddr.sin_port = htons(SRC_PORT);
    srcAddr.sin_addr.s_addr = inet_addr(SRC_IP);

    if(k_bind(sockfd, &destAddr, sizeof(destAddr), &srcAddr, sizeof(srcAddr)) == -1)
    {
        k_perror("Error in binding");
        exit(1);
    }
    printf("Socket bound\n");


    FILE *fp = fopen(filename, "w");
    if(fp == NULL)
    {
        perror("Error in opening file");
        exit(1);
    }

    char recvBuf[DATASIZE];
    int ret, cnt = 1;
    while(1)
    {
        while((ret = k_recvfrom(sockfd, recvBuf, DATASIZE, 0, &destAddr, NULL)) == -1)
        {
            if(globalEno != ENOMESSAGE)
            {
                k_perror("Error in receiving");
                exit(1);
            }
        }
        if(strcmp(recvBuf, "DONE") == 0) break;
        printf("Received packet %d\n", cnt++);
        fwrite(recvBuf, 1, ret, fp);
    }
    printf("Receiving Last packet..\n");
    fclose(fp);

    if(k_close(sockfd) == -1)
    {
        k_perror("Error in closing");
        exit(1);
    }

    return 0;
}