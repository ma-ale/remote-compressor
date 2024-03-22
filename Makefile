CFLAGS = -g -std=c99 -Wall -Wextra -pedantic

# regole di progetto

rcomp: rcomp_client rcomp_server

# regole file sorgente

rcomp_client: rcomp_client.o common.o

rcomp_server: rcomp_server.o common.o

rcomp_client.o: rcomp_client.c common.h

rcomp_server.o: rcomp_server.c common.h

common.o: common.c common.h

# pulisci la cartella di lavoro

clean:
	rm -f common.o rcomp_server rcomp_server.o rcomp_client rcomp_client.o
	rm -f *.tar.gz *.tar.bz2 folder-*