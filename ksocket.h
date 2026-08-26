/*
    Assignment 4 Submission
    Name: Pranav Jha
    Roll number: 22CS30061 
*/

#ifndef KSOCKET_H
#define KSOCKET_H

#include <arpa/inet.h>
#include <time.h>

#define MAX_SOCKETS 10
#define SOCK_KTP 2001
#define US 1000000
#define T 5000000 // in microseconds
#define SEL_WAIT 2500000 // in microseconds
#define PROBE_WAIT 1000000 // in microseconds
#define CLOSE_TIMER (10 * T) // currently 50 seconds, needs to be tuned based on probability
#define P 0.3  // probability

#define DATASIZE 512
#define MAXSEQNO 256
#define SENDBUFSIZE 10
#define RECVBUFSIZE 10
#define SENDWINDOWSIZE SENDBUFSIZE
#define RECVWINDOWSIZE RECVBUFSIZE

// errors
#define ENOSPACE 100
#define ENOTBOUND 101
#define ENOMESSAGE 102
#define EINVALID 103

// states
#define FREE 200
#define TO_CREATE 201
#define CREATED 202
#define TO_BIND 203
#define READY 204
#define TO_CLOSE 205

#define globalEno (*eno_location())

typedef struct {
    int seqNo; // seq no of data, or seq no of packet being acked
    int isAck; // is ack or data
    int dataLen; // length of data, window size for ack
    char data[DATASIZE]; // data
} packet;

typedef struct {
    int size; // max size of window possible
    int left, right; // left and right pointers
    int ack[MAXSEQNO];
} window;

typedef struct {
    char data[SENDBUFSIZE][DATASIZE]; // send buffer
    int dataLen[SENDBUFSIZE]; 
    int spaceLeft; // space left in the send buffer, for user to write, or read
    int notInWindow; // number of packets not in the window, but in the buffer
    int left; // left pointer, right pointer can be calculated from the corresponding left and right of the window
    int base; // base buffer, here the user will write or read from
} buffer;

typedef struct {
    int pid; // pid of the process that created this socket
    int sockfd; // UDP socket of this KTP socket
    int sockState; // state of the socket 0 - closed, 1 - open but not binded, 2 - binded
    struct sockaddr_in srcAddr; // source address
    socklen_t srcLen; // source address length
    struct sockaddr_in destAddr; // destination address
    socklen_t destLen; // destination address length
    /* main */
    window swnd;
    buffer sendBuffer;
    window rwnd;
    buffer recvBuffer;
    int noSpace; // no space left in the recv buffer
    struct timespec probeTime; // time when the probe packet was sent
    /* main end */
    struct timespec lastSent; // time when the send buffer was last sent
    int eno; // error number
    int transmissions; // number of transmissions
    int messages; // number of messages
} ktp_socket;

int *eno_location();
int k_socket(int domain, int type, int protocol);
int k_bind(int k_sockfd, struct sockaddr_in *destAddr, socklen_t destLen, struct sockaddr_in *srcAddr, socklen_t srcLen);
int k_sendto(int k_sockfd, const char *buf, int len, int flags, struct sockaddr_in *destAddr, socklen_t destLen);
int k_recvfrom(int k_sockfd, char *buf, int len, int flags, struct sockaddr_in *srcAddr, socklen_t *srcLen);
int k_close(int k_sockfd);
int k_perror(const char *s);

#endif

