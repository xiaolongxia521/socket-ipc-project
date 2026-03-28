CC = gcc
CFLAGS = -Wall -pthread
LDFLAGS = -lsqlite3
TARGETS = server client

all: $(TARGETS)

server: server.c database.c
	$(CC) $(CFLAGS) -o server server.c database.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f $(TARGETS) *.db

.PHONY: all clean
