#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int read_command(char *str, const char **com, const char **arg) {
	char *command = NULL, *argument = NULL, *tmp;
	char **saveptr;
	int argc = 0;

	// divido la stringa in token divisi da spazi, salvo il primo come comando
	// e il secondo come argomento
	for (; (tmp = strtok_r(str, " ", saveptr)) != NULL; argc++) {
		if (argc == 0) {
			command = tmp; 
		} else if (argc == 1) {
			argument = tmp;
		} else {
			fprintf(stderr, "Argomento di troppo: [%d] = '%s'\n", argc, tmp);
		}
	}

	*com = command;
	*arg = argument;
	// ritorno il numero di argomenti trovati
	return argc;
}

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

int connect_to_server(int sd, struct sockaddr_in *sa) {
	if (connect(sd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr, "Impossibile connettersi: %s\n", strerror(errno));
		return (-1);
	}
	return 0;
}

// aspetta un messaggio di risposta dal server, ritorna 0 se "OK"
// oppure negativo se ci sono stati errori o "NON OK"
int receive_response(int sd) {
	char resp[3];
	rcvd_bytes = recv(sd, &resp, 2, 0);
	if (rcvd_bytes < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return (-1);
	}
	resp[rcvd_bytes] = '\0';
	if (strcomp(resp, "OK") != 0) {
		fprintf(stderr, "Server segnala il seguente errore: %s\n", resp);
		return (-1);
	}
	printf("Ricevuto l'OK dal server/n");

	return 0;
}
