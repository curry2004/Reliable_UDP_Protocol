#/*
#    Assignment 4 Submission
#    Name: Pranav Jha
#    Roll number: 22CS30061 
#*/

# makefile to generate libsocket.a

CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -L. -lksocket

all: libksocket.a init user1 user2

library: libksocket.a

libksocket.a: ksocket.o
	ar rcs $@ $^

ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c $<

# makefile to create executable to run initksocket.c

init: initksocket.c libksocket.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

# make file to create user1 and user2 executables

user1: user1.c libksocket.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

user2: user2.c libksocket.a
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f *.o *.a init user1 user2