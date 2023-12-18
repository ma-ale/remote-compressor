#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int socket_stream(const char *addr_str, int port_no, int *sd, struct sockaddr_in *sa) {
	*sd = socket(AF_INET, SOCK_STREAM, 0);
	if (*sd < 0) {
		fprintf(stderr, "Impossibile creare il socket: %s\n", strerror(errno));
		return -1;
	}
	// conversione dell'indirizzo in formato numerico
	in_addr_t address;
	if (inet_pton(AF_INET, addr_str, (void *)&address) < 0) {
		fprintf(stderr, "Impossibile convertire l'indirizzo: %s\n", strerror(errno));
		return -1;
	}
	// preparazione della struttura contenente indirizzo IP e porta
	sa->sin_family		= AF_INET;
	sa->sin_port		= htons(port_no);
	sa->sin_addr.s_addr = address;

	return 0;
}

// attraverso la funzione "system()" comprime la cartella dei file del client (path)
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(int sd, const char *path, char alg) {
    char command [1024];

    if (alg == 'z'){
        snprintf(command, 1024, "tar -c -z -f archivio_compresso.tar.gz %s", path);
    } else if (alg == 'j'){
        snprintf(command, 1024, "tar -c -j -f archivio_compresso.tar.bz2 %s", path);
    } else {
        printf("Algoritmo non valido\n");
		return -1;
    }
    if(system(command) != 0){
        fprintf(stderr, "Impossibile fare la system() %s\n", strerror(errno));
		return -1;
    }
    return 0;
}
