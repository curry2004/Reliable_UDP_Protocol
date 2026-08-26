/*
    Assignment 4 Submission
    Name: Pranav Jha
    Roll number: 22CS30061 
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <errno.h>
#include <time.h>
#include "ksocket.h"

#define W(s) semop(s, &pop, 1) // wait(s)
#define S(s) semop(s, &vop, 1) // signal(s)

int key, key1, key2, mut, mut1, mut2, shmid;
struct sembuf pop, vop;
ktp_socket *sockets;

int eno = -1;

int *eno_location(void)
{
    return &eno;
}

int cmp_addr(struct sockaddr_in *a, struct sockaddr_in *b)
{
    if(a -> sin_family != b -> sin_family) return 0;
    if(a -> sin_port != b -> sin_port) return 0;
    if(a -> sin_addr.s_addr != b -> sin_addr.s_addr) return 0;
    return 1;
}

int copy_addr(struct sockaddr_in *a, struct sockaddr_in *b)
{
    a -> sin_family = b -> sin_family;
    a -> sin_port = b -> sin_port;
    a -> sin_addr.s_addr = b -> sin_addr.s_addr;
    return 0;
}

int timeDiff(struct timespec lastSent, int diff)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - lastSent.tv_sec) * US + (now.tv_nsec - lastSent.tv_nsec) / 1000 >= diff;
}

void initGet()
{
    // getting the semaphores initialised by initsocket.c from shared memory
    if((key = ftok("/", 'A')) == -1)
    {
        perror("initGet(): ftok key");
        exit(1);
    }
    if((key1 = ftok("/bin", 'B')) == -1)
    {
        perror("initGet(): ftok key1");
        exit(1);
    }
    if((key2 = ftok("/usr", 'C')) == -1)
    {
        perror("initGet(): ftok key 2");
        exit(1);
    }
    if((mut = semget(key, 1, 0777)) == -1)
    {
        perror("initGet(): semget mut");
        exit(1);
    }
    if((mut1 = semget(key1, 1, 0777)) == -1)
    {
        perror("initGet(): semget mut1");
        exit(1);
    }
    if((mut2 = semget(key2, 1, 0777)) == -1)
    {
        perror("initGet(): semget mut1");
        exit(1);
    }
    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1; // decrement
    vop.sem_op = 1; // increment
    // getting the shared memory initialised by initsocket.c, if not exits then error
    if((shmid = shmget(key, MAX_SOCKETS * sizeof(ktp_socket), 0777)) == -1)
    {
        perror("initGet(): shmget shmid");
        exit(1);
    }
    // attaching shared memory
    if((sockets = (ktp_socket *)shmat(shmid, 0, 0)) == (void *)-1)
    {
        perror("initGet(): shmat sockets");
        exit(1);
    }
    return;
}

void initRem()
{
    // removing the shared memory
    if(shmdt(sockets) == -1)
    {
        perror("initRem(): shmdt sockets");
        exit(1);
    }
    return;
}

// domain should be AF_INET, type SOCK_KTP, protocol should be 0
int k_socket(int domain, int type, int protocol)
{
    if(type != SOCK_KTP)
    {
        eno = EINVALID;
        initRem();
        return -1;
    }

    initGet();
    W(mut);
    for(int i = 0; i < MAX_SOCKETS; i++)
    {
        if(sockets[i].sockState == FREE)
        {
            sockets[i].sockState = TO_CREATE;
            S(mut1);
            W(mut2);
            if(sockets[i].sockState != CREATED)
            {
                eno = sockets[i].eno;
                fflush(stdout);
                S(mut);
                initRem();
                return -1;
            }

            sockets[i].pid = getpid();
            sockets[i].lastSent.tv_sec = 0;
            sockets[i].lastSent.tv_nsec = 0;
            sockets[i].transmissions = sockets[i].messages = 0;
            sockets[i].eno = -1;
            
            // initialising the window structures
            sockets[i].swnd.left = sockets[i].rwnd.left = 0;
            sockets[i].swnd.right = sockets[i].rwnd.right = 0;
            sockets[i].swnd.size = SENDWINDOWSIZE;
            sockets[i].rwnd.size = RECVWINDOWSIZE;
            sockets[i].noSpace = 0;
            sockets[i].probeTime.tv_sec = 0;
            sockets[i].probeTime.tv_nsec = 0;
            
            // initialising the buffer variables
            sockets[i].sendBuffer.spaceLeft = SENDBUFSIZE;
            sockets[i].sendBuffer.notInWindow = 0;
            sockets[i].recvBuffer.spaceLeft = RECVBUFSIZE;
            sockets[i].recvBuffer.notInWindow = 0;
            sockets[i].sendBuffer.left = sockets[i].recvBuffer.left = 0;
            sockets[i].sendBuffer.base = sockets[i].recvBuffer.base = 0;

            S(mut);
            initRem();
            return i;
        }
    }

    eno = ENOSPACE;
    S(mut);
    initRem();
    return -1;
}

// binding the socket to the destination address and source address, should be not NULL
int k_bind(int k_sockfd, struct sockaddr_in *destAddr, socklen_t destLen, struct sockaddr_in *srcAddr, socklen_t srcLen)
{
    initGet();
    W(mut);

    if(k_sockfd < 0 || k_sockfd >= MAX_SOCKETS || sockets[k_sockfd].sockState != CREATED)
    {
        eno = EINVALID;
        S(mut);
        initRem();
        return -1;
    }

    // copying the addresses
    copy_addr(&sockets[k_sockfd].destAddr, destAddr);
    sockets[k_sockfd].destLen = destLen;
    copy_addr(&sockets[k_sockfd].srcAddr, srcAddr);
    sockets[k_sockfd].srcLen = srcLen;


    sockets[k_sockfd].sockState = TO_BIND;
    S(mut1);
    W(mut2);
    if(sockets[k_sockfd].sockState != READY)
    {
        eno = sockets[k_sockfd].eno;
        S(mut);
        initRem();
        return -1;
    }

    S(mut);
    initRem();
    return 0;
}

// sending the message to the destination address, should be bound, and buf and destAddr should not be NULL
int k_sendto(int k_sockfd, const char *buf, int len, int flags, struct sockaddr_in *destAddr, socklen_t destLen)
{
    initGet();
    W(mut);

    // checking errors
    if(k_sockfd < 0 || k_sockfd >= MAX_SOCKETS || sockets[k_sockfd].sockState != READY)
    {
        eno = EINVALID;
        S(mut);
        initRem();
        return -1;
    }
    // checking if socket it allowed to send on this address
    if(!cmp_addr(&sockets[k_sockfd].destAddr, destAddr) || sockets[k_sockfd].destLen != destLen)
    {
        eno = ENOTBOUND;
        S(mut);
        initRem();
        return -1;
    }
    // checking if buffer full
    if(sockets[k_sockfd].sendBuffer.spaceLeft == 0)
    {
        eno = ENOSPACE;
        S(mut);
        initRem();
        return -1;
    }

    len = (len > DATASIZE) ? DATASIZE : len;
    for(int i = 0; i < len; i++) sockets[k_sockfd].sendBuffer.data[sockets[k_sockfd].sendBuffer.base][i] = buf[i];
    sockets[k_sockfd].sendBuffer.dataLen[sockets[k_sockfd].sendBuffer.base] = len;
    sockets[k_sockfd].sendBuffer.base = (sockets[k_sockfd].sendBuffer.base + 1) % SENDBUFSIZE;
    sockets[k_sockfd].sendBuffer.spaceLeft--;
    sockets[k_sockfd].sendBuffer.notInWindow++;
    sockets[k_sockfd].messages++;

    S(mut);
    initRem();
    return len;
}

// receiving the message from the source addr, can be NULL, but buf should not be NULL
int k_recvfrom(int k_sockfd, char *buf, int len, int flags, struct sockaddr_in *srcAddr, socklen_t *srcLen)
{
    initGet();
    W(mut);

    // checking errors
    if(k_sockfd < 0 || k_sockfd >= MAX_SOCKETS || sockets[k_sockfd].sockState != READY)
    {
        eno = EINVALID;
        S(mut);
        initRem();
        return -1;
    }

    // checking if buffer has something to read
    if(sockets[k_sockfd].recvBuffer.notInWindow == 0)
    {
        eno = ENOMESSAGE;
        S(mut);
        initRem();
        return -1;
    }

    int temp = sockets[k_sockfd].recvBuffer.base;    
    len = (len > sockets[k_sockfd].recvBuffer.dataLen[temp]) ? sockets[k_sockfd].recvBuffer.dataLen[temp] : len;
    for(int i = 0; i < len; i++) buf[i] = sockets[k_sockfd].recvBuffer.data[temp][i];
    sockets[k_sockfd].recvBuffer.base = (sockets[k_sockfd].recvBuffer.base + 1) % RECVBUFSIZE;
    sockets[k_sockfd].recvBuffer.notInWindow--;
    sockets[k_sockfd].recvBuffer.spaceLeft++;
    sockets[k_sockfd].rwnd.size++; // updating the window size

    S(mut);
    initRem();
    return len;
}

int k_close(int k_sockfd)
{
    initGet();
    W(mut);

    // checking errors
    if(k_sockfd < 0 || k_sockfd >= MAX_SOCKETS || sockets[k_sockfd].sockState == FREE)
    {
        eno = EINVALID;
        S(mut);
        initRem();
        return -1;
    }
    S(mut);

    struct timespec temp;
    clock_gettime(CLOCK_MONOTONIC, &temp);
    while(1)
    {
        W(mut);
        if(sockets[k_sockfd].sendBuffer.spaceLeft == SENDBUFSIZE || timeDiff(temp, CLOSE_TIMER)) break;
        S(mut);
        usleep(1000);
    }

    sockets[k_sockfd].sockState = TO_CLOSE;
    S(mut1);
    W(mut2);

    if(sockets[k_sockfd].sockState != FREE)
    {
        eno = sockets[k_sockfd].eno;
        S(mut);
        initRem();
        return -1;
    }

    S(mut);
    initRem();
    return 0;
}

int k_perror(const char *s)
{
    // if eno is not set, then no error
    if(eno == -1) return 0;
    // printing the error message
    fprintf(stderr, "%s: ", s);
    switch(eno)
    {
        case ENOSPACE:
            fprintf(stderr, "No space left on device\n");
            break;
        case ENOTBOUND:
            fprintf(stderr, "Socket is not bound\n");
            break;
        case ENOMESSAGE:
            fprintf(stderr, "No message available\n");
            break;
        case EINVALID:
            fprintf(stderr, "Something is Invalid\n");
            break;
        default:
            errno = eno;
            perror("");
    }
    return 1;
}
