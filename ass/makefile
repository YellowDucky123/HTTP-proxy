CC=gcc
CFLAGS=-I.

script: proxy.o linkedlist.o connection.o util.o
	$(CC) -o proxy proxy.o linkedlist.o connection.o util.o

proxy.o: proxy.c
	$(CC) $(CFLAGS) -c proxy.c 

linkedlist.o: linkedlist.c
	$(CC) $(CFLAGS) -c linkedlist.c

connection.o: connection.c
	$(CC) $(CFLAGS) -c connection.c

util.o: util.c
	$(CC) $(CFLAGS) -c util.c