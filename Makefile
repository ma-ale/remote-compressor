CFLAGS = -g -std=c99 -Wall -Wextra -pedantic

# regole di progetto

esercizio: server/server_esercizio client/client_esercizio

rcomp: client/rcomp_client server/rcomp_server

# regole file sorgente

client/rcomp_client: client/rcomp_client.o common.o

server/rcomp_server: server/rcomp_server.o common.o

client/rcomp_client.o: client/rcomp_client.c

server/rcomp_server.o: server/rcomp_server.c

common.o: common.c

server/server_esercizio: server/server_esercizio.c

client/client_esercizio: client/client_esercizio.c