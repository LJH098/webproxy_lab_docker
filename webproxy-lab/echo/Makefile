CC = gcc
CFLAGS = -g -Wall -I..

all: echoserver echoclient

echoserver: echoserver.c ../csapp.c ../csapp.h
	$(CC) $(CFLAGS) echoserver.c ../csapp.c -o echoserver

echoclient: echoclient.c ../csapp.c ../csapp.h
	$(CC) $(CFLAGS) echoclient.c ../csapp.c -o echoclient

clean:
	rm -f echoserver echoclient *.o
