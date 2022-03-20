CC=gcc
CFLAGS=-Wall
LDFLAGS=-ldl

prog : tesh.c
	$(CC) -o tesh $(CFLAGS) tesh.c $(LDFLAGS)
