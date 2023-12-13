#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// stampa le funzioni disponibili
void help(void)
{
	printf("Comandi disponibili:\n"
	       "help:\n --> mostra l'elenco dei comandi disponibili\n"
	       "add [file]\n --> invia il file specificato al server remoto\n"
	       "compress [alg]\n --> riceve dal server remoto "
	       "l'archivio compresso secondo l'algoritmo specificato\n"
	       "quit\n --> disconnessione\n");
}

// connettiti al server all'indizzo e porta specificata
int connect_to_server(const char *addr, int port, int *sd, struct sockaddr_in *sa)
{
	return 0;
}

// manda un comando testuale al server come "quit" e "compress"
int send_command(int sd, const char *str);

// manda un file al server specificando il suo percorso
int send_file(int sd, const char *path) { return 0; }

int main(int argc, char **argv)
{

	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta

	return 0;
}