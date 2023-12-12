#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdio.h>

int main(void) {

    const int l = 10;
	char comando[l];
	const int i = 10;
	char arg[i];
	int argno;

	while (1) {
		printf("rcomp> ");
		argno = scanf("%s", comando);

	    if(strcmp(comando, "help") == 0){
	        printf("Comandi disponibili:\n"
	        "help:\n --> mostra l'elenco dei comandi disponibili\n"
	        "add [file]\n --> invia il file specificato al server remoto\n"
	        "compress [alg]\n --> riceve dal server remoto l'archivio compresso secondo l'algoritmo specificato\n"
	        "quit\n --> disconnessione\n");
	    }
	    else if(strcmp(comando, "quit") == 0){
	        // manda il qcomando quit al server
	       // close(sd); //chiude la connessione
	        return EXIT_SUCCESS;
	    }
	    else
	        printf("Comando non esistente\n");
    }

    return 0;

}
