CFLAGS = -g -std=c99 -Wall -Wextra -pedantic

all: client/client server/server

client/client: client/client.c

server/server: server/server.c

clean:
	rm -f client server