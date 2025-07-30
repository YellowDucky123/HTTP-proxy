CC=gcc
CFLAGS=-I.

script: proxy.o linkedlist.o connection.o util.o cache.o dll.o ht.o
	$(CC) -o proxy proxy.o linkedlist.o connection.o util.o cache.o dll.o ht.o

proxy.o: proxy.c
	$(CC) $(CFLAGS) -c proxy.c 

linkedlist.o: linkedlist.c
	$(CC) $(CFLAGS) -c linkedlist.c

connection.o: connection.c
	$(CC) $(CFLAGS) -c connection.c

util.o: util.c
	$(CC) $(CFLAGS) -c util.c

cache.o: cache/cache.c
	$(CC) $(CFLAGS) -c cache/cache.c

dll.o: cache/dll.c
	$(CC) $(CFLAGS) -c cache/dll.c

ht.o: cache/ht.c
	$(CC) $(CFLAGS) -c cache/ht.c