/*
    Assignment 4 Submission
    Name: Pranav Jha
    Roll number: 22CS30061 
*/

#include <bits/types/struct_timeval.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <time.h>
#include <errno.h>
#include <pthread.h>
#include "ksocket.h"

#define W(s) semop(s, &pop, 1) // wait(s)
#define S(s) semop(s, &vop, 1) // signal(s)

packet sendPkt, recvPkt; // packet to send and receive

int shmid, key, key1, key2, mut, mut1, mut2;
struct sembuf pop, vop;
ktp_socket *sockets;
pthread_t recvThread_id, sendThread_id, garbageThread_id, sockBindCloseThread_id;

void clean() // TODO: remove this
{
    shmdt(sockets);
    shmctl(shmid, IPC_RMID, 0);

    // remove semaphore
    semctl(mut, 0, IPC_RMID);
    semctl(mut1, 0, IPC_RMID);
    semctl(mut2, 0, IPC_RMID);
    return;
}

void init()
{
    // initialise semaphore
    key = ftok("/", 'A');
    key1 = ftok("/bin", 'B'); key2 = ftok("/usr", 'C');
    if(key == -1 || key1 == -1 || key2 == -1)
    {
        perror("Error in generating key");
        clean();
        exit(1);
    }
    mut = semget(key, 1, IPC_EXCL | IPC_CREAT | 0777);
    if(mut == -1)
    {
        perror("Semaphore already exists");
        clean();
        exit(1);
    }
    mut1 = semget(key1, 1, IPC_EXCL | IPC_CREAT | 0777);
    if(mut1 == -1)
    {
        perror("Semaphore 1 already exists");
        clean();
        exit(1);
    }
    mut2 = semget(key2, 1, IPC_EXCL | IPC_CREAT | 0777);
    if(mut2 == -1)
    {
        perror("Semaphore 2 already exists");
        clean();
        exit(1);
    }

    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; // decrement
    vop.sem_op = 1; // increment
    semctl(mut, 0, SETVAL, 1); // setting mutex to 1
    if(semctl(mut, 0, GETVAL) != 1)
    {
        perror("Error in setting mutex");
        clean();
        exit(1);
    }

    semctl(mut1, 0, SETVAL, 0); // setting mutex 1 to 0
    if(semctl(mut1, 0, GETVAL) != 0)
    {
        perror("Error in setting mutex 1");
        clean();
        exit(1);
    }

    semctl(mut2, 0, SETVAL, 0); // setting mutex 2 to 1
    if(semctl(mut2, 0, GETVAL) != 0)
    {
        perror("Error in setting mutex 2");
        clean();
        exit(1);
    }

    // initialise KTP sockets in shm
    shmid = shmget(key, MAX_SOCKETS * sizeof(ktp_socket), IPC_EXCL | IPC_CREAT | 0777);
    if(shmid == -1)
    {
        perror("Shared memory already exists");
        clean();
        exit(1);
    }

    sockets = (ktp_socket *)shmat(shmid, 0, 0); // attaching shared memory
    if(sockets == (void *)-1)
    {
        perror("Error in attaching shared memory");
        clean();
        exit(1);
    }
    for(int i = 0; i < MAX_SOCKETS; i++)
    {
        sockets[i].sockState = FREE;
    }
}

void parHandler(int signum)
{
    clean();
    exit(0);
}

int timeDiff(struct timespec lastSent, int diff)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - lastSent.tv_sec) * US + (now.tv_nsec - lastSent.tv_nsec) / 1000 >= diff;
}

int dropMessage(float p)
{
    // generated random number between 0 and 1
    float r = (float)rand() / (float)RAND_MAX;
    return r < p;
}

void printPacket(packet p)
{
    if(p.isAck)
    {
        printf("ACK[seqNo = %d, rwnd = %d]", p.seqNo + 1, p.dataLen);
        fflush(stdout);
    }
    else
    {
        printf("DATA[seqNo = %d, dataLen = %d]", p.seqNo + 1, p.dataLen);
        fflush(stdout);
    }
    return;
}

void printTab(int n)
{
    for(int i = 0; i < n; i++) printf("\t");
    fflush(stdout);
    return;
}

void sendAck(int i)
{
    sendPkt.seqNo = (sockets[i].rwnd.left + MAXSEQNO - 1) % MAXSEQNO;
    sendPkt.isAck = 1;
    sendPkt.dataLen = sockets[i].rwnd.size;
    if(sendto(sockets[i].sockfd, (void *)&sendPkt, sizeof(packet), 0, (struct sockaddr *)&sockets[i].destAddr, sockets[i].destLen) < 0)
    {
        perror("initksocket.c: sendto error");
        clean();
        exit(1);
    }

    return;
}

/*
    -The recvThread will receive the packets from the sockets and send ACKs for the packets received.
    -It will also drop packets with a probability P.
    -It follows the stop and wait protocol.
    -When a packet is received, it is checked if it is the expected packet, then it is taken, else it is dropped.
     and ACK is sent for the packet received either way.
    -The ACK is sent with the sequence number of the packet received.
    -If buffer is full or wrong ACK is received, the packet is dropped.
*/
void *recvThread(void *arg)
{
    fd_set readfds;
    struct timeval tv;

    while(1)
    {
        W(mut);
        FD_ZERO(&readfds);
        int maxfd = -1;
        for(int i = 0; i < MAX_SOCKETS; i++)
        {
            if(sockets[i].sockState == READY)
            {
                FD_SET(sockets[i].sockfd, &readfds);
                if(sockets[i].sockfd > maxfd)
                {
                    maxfd = sockets[i].sockfd;
                }
            }
        }
        tv.tv_sec = SEL_WAIT / US;
        tv.tv_usec = SEL_WAIT % US;
        S(mut);

        if(select(maxfd + 1, &readfds, NULL, NULL, &tv) < 0)
        {
            perror("initsocket.c: Error in select\n");
            clean();
            exit(1);
        }

        W(mut);
        for(int i = 0; i < MAX_SOCKETS; i++)
        {
            /* probe packet */
            if(sockets[i].sockState == READY && sockets[i].noSpace && sockets[i].rwnd.size && timeDiff(sockets[i].probeTime, PROBE_WAIT))
            {
                // sending the ACK
                sendAck(i);

                // sockets[i].noSpace = 0; // TODO: check if this is correct
                clock_gettime(CLOCK_MONOTONIC, &sockets[i].probeTime);
                
                printTab(i);
                printf("Socket %d: SENT ", i);
                printPacket(sendPkt);
                printf(" (probe packet)\n");
                fflush(stdout);
            } 

            /* check which ready socket received the message and process it */
            if(sockets[i].sockState == READY && FD_ISSET(sockets[i].sockfd, &readfds))
            {
                // receiving the packet
                if(recvfrom(sockets[i].sockfd, (void *)&recvPkt, sizeof(packet), 0, (struct sockaddr *)&sockets[i].destAddr, &sockets[i].destLen) < 0)
                {
                    perror("initksocket.c: recvfrom error");
                    clean();
                    exit(1);
                }

                
                // dropping the packet with probability P
                if(dropMessage(P))
                {
                    printTab(i);
                    printf("Socket %d: DROPPED ", i);
                    printPacket(recvPkt);
                    printf(" (due to probability)\n");
                    fflush(stdout);
                    continue;
                }
                
                printTab(i);
                printf("Socket %d: RECEIVED ", i);
                printPacket(recvPkt);
                printf("\n");
                fflush(stdout);

                // if the packet is ACK
                if(recvPkt.isAck)
                {
                    // if the ack seq no is within the send window, take the ack
                    int dr = (sockets[i].swnd.right - sockets[i].swnd.left + MAXSEQNO) % MAXSEQNO; // where the current right pointer is
                    int dp = (recvPkt.seqNo - sockets[i].swnd.left + MAXSEQNO) % MAXSEQNO; // incoming packet distance
                    if(dp < dr)
                    {   
                        // move the left pointer to the right
                        int temp = 0;
                        while(temp <= dp)
                        {
                            sockets[i].swnd.left = (sockets[i].swnd.left + 1) % MAXSEQNO;
                            sockets[i].sendBuffer.left = (sockets[i].sendBuffer.left + 1) % SENDBUFSIZE;
                            sockets[i].sendBuffer.spaceLeft++;
                            temp++;
                        }
                    }
                    else
                    {
                        printTab(i);
                        printf("Socket %d: IGNORED ", i);
                        printPacket(recvPkt);
                        printf(" (out of send window), SWND[left = %d, right = %d]\n", sockets[i].swnd.left + 1, sockets[i].swnd.right + 1);
                        fflush(stdout);
                    }
                    
                    // updating send window, if swnd size is more than the recv packet size, update the swnd size
                    if(sockets[i].swnd.size > recvPkt.dataLen)
                    {
                        sockets[i].swnd.size = recvPkt.dataLen;
                        // move the right pointer to the left, if the right pointer is out of the window
                        int currSize = (sockets[i].swnd.right - sockets[i].swnd.left + MAXSEQNO) % MAXSEQNO;
                        while(currSize > sockets[i].swnd.size)
                        {
                            sockets[i].swnd.right = (sockets[i].swnd.right + MAXSEQNO - 1) % MAXSEQNO;
                            sockets[i].sendBuffer.notInWindow++;
                            currSize--;
                        }
                    }
                    else
                    {
                        sockets[i].swnd.size = recvPkt.dataLen > SENDWINDOWSIZE ? SENDWINDOWSIZE : recvPkt.dataLen;
                        // update the right pointer to the right
                        int currSize = (sockets[i].swnd.right - sockets[i].swnd.left + MAXSEQNO) % MAXSEQNO;
                        while(currSize < sockets[i].swnd.size && sockets[i].sendBuffer.notInWindow > 0)
                        {
                            sockets[i].swnd.right = (sockets[i].swnd.right + 1) % MAXSEQNO;
                            sockets[i].sendBuffer.notInWindow--;
                            currSize++;
                        }
                    }

                    printTab(i);
                    printf("Socket %d: UPDATED SWND[left = %d, right = %d, size = %d]\n", i, sockets[i].swnd.left + 1, sockets[i].swnd.right + 1, sockets[i].swnd.size);
                    fflush(stdout);
                }
                else // if the packet is DATA
                {
                    // if the packet is the expected packet, but the buffer is full, drop the packet
                    if(sockets[i].rwnd.size == 0)
                    {
                        printTab(i);
                        printf("Socket %d: DROPPED ", i);
                        printPacket(recvPkt);
                        printf(" (recv buffer full)\n");
                        fflush(stdout);
                        continue;
                    }

                    // update the nospace flag, as it sent only because it know space is there
                    if(sockets[i].noSpace == 1) sockets[i].noSpace = 0;

                    int dr = (sockets[i].rwnd.right - sockets[i].rwnd.left + MAXSEQNO) % MAXSEQNO; // where the current right pointer is
                    int dp = (recvPkt.seqNo - sockets[i].rwnd.left + MAXSEQNO) % MAXSEQNO; // incoming packet distance
                    // if space in the buffer, take the packet
                    if(dp < sockets[i].rwnd.size) // if the packet is in the window
                    {
                        // if the packet is not already received, take the packet
                        if(sockets[i].rwnd.ack[recvPkt.seqNo] == 0)
                        {
                            // checking if right pointers needs to be changed
                            if(dp >= dr) // if the packet is not in the window
                            {
                                sockets[i].rwnd.right = (sockets[i].rwnd.left + dp + 1) % MAXSEQNO;
                                sockets[i].recvBuffer.spaceLeft += dp - dr;
                            }
                            
                            // updating the window buffer
                            sockets[i].rwnd.ack[recvPkt.seqNo] = 1;
                            int buffr = (sockets[i].recvBuffer.left + dp) % SENDBUFSIZE;
                            sockets[i].recvBuffer.dataLen[buffr] = recvPkt.dataLen;
                            for(int j = 0; j < recvPkt.dataLen; j++) sockets[i].recvBuffer.data[buffr][j] = recvPkt.data[j];
                        
                            // move the left pointer to skip the already received packets
                            while(sockets[i].rwnd.ack[sockets[i].rwnd.left] == 1)
                            {
                                fflush(stdout);
                                sockets[i].rwnd.ack[sockets[i].rwnd.left] = 0;
                                sockets[i].rwnd.left = (sockets[i].rwnd.left + 1) % MAXSEQNO;
                                sockets[i].recvBuffer.left = (sockets[i].recvBuffer.left + 1) % SENDBUFSIZE;
                                sockets[i].recvBuffer.notInWindow++;
                                sockets[i].rwnd.size--;
                            }

                            // setting the noSpace flag
                            if(sockets[i].rwnd.size == 0) sockets[i].noSpace = 1;

                            // sending the ACK
                            sendAck(i);

                            printTab(i);
                            printf("Socket %d: SENT ", i);
                            printPacket(sendPkt);
                            printf("\n");
                            fflush(stdout);
                        }
                        else
                        {
                            // sending the ACK, of last received packet
                            sendAck(i);

                            printTab(i);
                            printf("Socket %d: SENT ", i);
                            printPacket(sendPkt);
                            printf(" (duplicate packet, sending last received)\n");
                            fflush(stdout);
                        }
                    }
                    else
                    {
                        // send ACK of last received packet
                        sendAck(i);

                        printTab(i);
                        printf("Socket %d: SENT ", i);
                        printPacket(sendPkt);
                        printf(" (out of recv window, sending last received)\n");
                        fflush(stdout);
                    }
                }
            }
        }
        S(mut);
    }

    printf("Receiver exiting\n");
    return NULL;
}

/*
    -The sendThread will send the packets in the send buffer of the sockets.
    -It will send the packets in the buffer if the last packet was sent T/2 seconds ago.
    -It will send the packet with the sequence number of the packet.
    -It will also increment the sequence number of the packet.
    -It will also increment the number of transmissions of the packet.
    -On timeout, the packet will be sent again.
*/
void *sendThread(void *arg)
{
    while(1)
    {
        struct timespec temp;
        temp.tv_sec = (T / 2) / US;
        temp.tv_nsec = ((T / 2) % US) * 1000;
        nanosleep(&temp, NULL); // sleep for T / 2 seconds

        W(mut);
        // send the packets in current window if timeout happens
        for(int i = 0; i < MAX_SOCKETS; i++)
        {
            /* TODO: send thread */
            // check for timeout, in the messages currently in the window buffer
            if(sockets[i].sockState == READY && timeDiff(sockets[i].lastSent, T))
            {
                // checking if any message in the window buffer needs to be sent
                int l = sockets[i].swnd.left, r = sockets[i].swnd.right, buffl = sockets[i].sendBuffer.left;
                while(l != r)
                {
                    // sending all the packets

                    // making the packet
                    sendPkt.seqNo = l;
                    sendPkt.isAck = 0;
                    sendPkt.dataLen = sockets[i].sendBuffer.dataLen[buffl];
                    for(int j = 0; j < sendPkt.dataLen; j++) sendPkt.data[j] = sockets[i].sendBuffer.data[buffl][j];
                    
                    // sending the packet
                    if(sendto(sockets[i].sockfd, (void *)&sendPkt, sizeof(packet), 0, (struct sockaddr *)&sockets[i].destAddr, sockets[i].destLen) < 0)
                    {
                        perror("initksocket.c: sendto error");
                        clean();
                        exit(1);
                    }
                    sockets[i].transmissions++;
                    clock_gettime(CLOCK_MONOTONIC, &sockets[i].lastSent);

                    printTab(i);
                    printf("Socket %d: SENT ", i);
                    printPacket(sendPkt);
                    printf(" (timeout)\n");
                    fflush(stdout);

                    l = (l + 1) % MAXSEQNO;
                    buffl = (buffl + 1) % SENDBUFSIZE;
                }
            }

            // checking the send buffer for the next packet to be sent
            if(sockets[i].sockState == READY)
            {
                int rbuff = (sockets[i].sendBuffer.left + ((sockets[i].swnd.right - sockets[i].swnd.left + MAXSEQNO) % MAXSEQNO)) % SENDBUFSIZE;
                while(((sockets[i].swnd.right - sockets[i].swnd.left + MAXSEQNO) % MAXSEQNO) < sockets[i].swnd.size && sockets[i].sendBuffer.notInWindow > 0)
                {
                    // making the packet
                    sendPkt.seqNo = sockets[i].swnd.right;
                    sendPkt.isAck = 0;
                    sendPkt.dataLen = sockets[i].sendBuffer.dataLen[rbuff];
                    for(int j = 0; j < sendPkt.dataLen; j++) sendPkt.data[j] = sockets[i].sendBuffer.data[rbuff][j];

                    // updating swnd variables
                    sockets[i].swnd.right = (sockets[i].swnd.right + 1) % MAXSEQNO;
                    rbuff = (rbuff + 1) % SENDBUFSIZE;
                    sockets[i].sendBuffer.notInWindow--;

                    // sending the packet
                    if(sendto(sockets[i].sockfd, (void *)&sendPkt, sizeof(packet), 0, (struct sockaddr *)&sockets[i].destAddr, sockets[i].destLen) < 0)
                    {
                        perror("initksocket.c: sendto error");
                        clean();
                        exit(1);
                    }
                    sockets[i].transmissions++;
                    clock_gettime(CLOCK_MONOTONIC, &sockets[i].lastSent);

                    printTab(i);
                    printf("Socket %d: SENT ", i);
                    printPacket(sendPkt);
                    printf(" (next packet)\n");
                    fflush(stdout);
                }
            }
        }
        S(mut);
    }

    printf("Sender exiting\n");
    return NULL;
}

void *garbageThread(void *arg)
{
    // if pid of the socket is dead and it has not been freed for a long time, free it
    while(1)
    {
        struct timespec temp;
        temp.tv_sec = (T / 2) / US;
        temp.tv_nsec = ((T / 2) % US) * 1000;
        nanosleep(&temp, NULL); // sleep for T / 2 seconds

        W(mut);
        for(int i = 0; i < MAX_SOCKETS; i++)
        {
            /* free the ith k_socket if possible */
            if(sockets[i].sockState != FREE && kill(sockets[i].pid, 0) < 0 && errno == ESRCH)
            {
                if((close(sockets[i].sockfd)) < 0)
                {
                    printf("initksocket.c: Error in closing socket %d\n", i);
                    perror("");
                    fflush(stdout);
                    continue;
                }
                sockets[i].sockState = FREE;

                printTab(i);
                printf("Socket %d: CLOSED (by garbage collector)\n\n", i);
                fflush(stdout);
            }
        }
        S(mut);
    }

    printf("Garbage collector exiting\n");
    fflush(stdout);
    return NULL;
}

/*
    -Helper thread to create, bind and close the sockets.
    -Responds to ksocket, kbind and kclose calls.
*/
void *sockBindCloseThread(void *arg)
{
    while(1)
    {
        W(mut1);
        for(int i = 0; i < MAX_SOCKETS; i++)
        {
            int flagerr = 0;
            if(sockets[i].sockState == TO_CREATE)
            {
                /* create a socket */
                sockets[i].sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                if(sockets[i].sockfd < 0)
                {
                    flagerr = 1;
                    sockets[i].eno = errno;
                }
                if(!flagerr)
                {
                    sockets[i].sockState = CREATED;
                    
                    printTab(i);
                    printf("Socket %d: CREATED\n\n", i);
                    fflush(stdout);
                }
                break;
            }
            else if(sockets[i].sockState == TO_BIND)
            {
                /* bind the socket */
                if((bind(sockets[i].sockfd, (struct sockaddr *)&sockets[i].srcAddr, sockets[i].srcLen)) < 0)
                {
                    flagerr = 1;
                    sockets[i].eno = errno;
                }
                if(!flagerr)
                {
                    sockets[i].sockState = READY;

                    printTab(i);
                    printf("Socket %d: BOUND to %s:%d\n\n", i, inet_ntoa(sockets[i].srcAddr.sin_addr), ntohs(sockets[i].srcAddr.sin_port));
                    fflush(stdout);
                }
                break;
            }
            else if(sockets[i].sockState == TO_CLOSE)
            {
                /* close the socket */
                printf("Socket %d Stats: No of messages: %d, No of transmissions: %d\n", i, sockets[i].messages, sockets[i].transmissions);
                fflush(stdout);
                if((close(sockets[i].sockfd)) < 0)
                {
                    flagerr = 1;
                    sockets[i].eno = errno;
                }
                if(!flagerr)
                {
                    sockets[i].sockState = FREE;

                    printTab(i);
                    printf("Socket %d: CLOSED\n\n", i);
                    fflush(stdout);
                }
                break;
            }
        }
        S(mut2);
    }

    return NULL;
}

int main()
{
    init();

    signal(SIGINT, parHandler);
    srand(time(0));

    // creating threads
    pthread_create(&recvThread_id, NULL, recvThread, NULL);
    pthread_create(&sendThread_id, NULL, sendThread, NULL);
    pthread_create(&garbageThread_id, NULL, garbageThread, NULL);
    pthread_create(&sockBindCloseThread_id, NULL, sockBindCloseThread, NULL);

    pthread_join(recvThread_id, NULL);
    pthread_join(sendThread_id, NULL);
    pthread_join(garbageThread_id, NULL);
    pthread_join(sockBindCloseThread_id, NULL);

    // detach and remove shared memory
    shmdt(sockets);
    shmctl(shmid, IPC_RMID, 0);

    // remove semaphore
    semctl(mut, 0, IPC_RMID);
    semctl(mut1, 0, IPC_RMID);
    semctl(mut2, 0, IPC_RMID);

    clean();
    return 0;
}