CC = gcc
CFLAGS = -Wall -pthread
LDFLAGS = -lsqlite3 -lsystemd
TARGETS = server client dbus-service

all: $(TARGETS)

server: server.c dbus-client.c dbus-client.h database.c database.h
	$(CC) $(CFLAGS) -o server server.c dbus-client.c database.c $(LDFLAGS)

dbus-service: dbus-service.c database.c database.h
	$(CC) $(CFLAGS) -o dbus-service dbus-service.c database.c $(LDFLAGS)

client: client.c
	$(CC) $(CFLAGS) -o client client.c

clean:
	rm -f $(TARGETS) *.db server.log *.o

.PHONY: all clean
