/*
 * client_v3
 *
 * Il programma client:
 * - apre un socket di tipo stream
 * - ciclicamente:
 * --- legge una stringa da stdin
 * --- la invia al server
 * --- attende la risposta dal server
 * - quando da stdin viene letta la stringa 'exit' termina
 *
 */

/*
	Scrivere un programma client che:

	1) apre un socket di tipo connection-oriented
	2) ciclicamente:
	2a) legge una linea da stdin
	2b) invia la lunghezza della linea al server
	2c) invia la linea al server
	2d) attende la risposta dal server
	2e) se l'utente ha inserito 'exit', termina. Altrimenti, torna al punto 2a
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int socket_stream(const char *addr_str, int port_no, int *sd, struct sockaddr_in *sa)
{
	*sd = socket(AF_INET, SOCK_STREAM, 0);
	if (*sd < 0) {
		fprintf(
		    stderr, "Impossibile creare il socket: %s\n", strerror(errno)
		);
		return -1;
	}
	// conversione dell'indirizzo in formato numerico
	in_addr_t address;
	if (inet_pton(AF_INET, addr_str, (void *)&address) < 0) {
		fprintf(
		    stderr,
		    "Impossibile convertire l'indirizzo: %s\n",
		    strerror(errno)
		);
		return -1;
	}
	// preparazione della struttura contenente indirizzo IP e porta
	sa->sin_family	    = AF_INET;
	sa->sin_port	    = htons(port_no);
	sa->sin_addr.s_addr = address;

	return 0;
}

int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	// TODO: prendi l'indirizzo e la porta da riga di comando
	const char *addr_str = "127.0.0.1";
	const int   port_no  = 10000;

	// creo il socket connection-oriented
	int		   sd;
	struct sockaddr_in sa;
	if (socket_stream(addr_str, port_no, &sd, &sa) < 0) {
		exit(EXIT_FAILURE);
	}

	// --- CONNESSIONE --- //
	printf("Connessione...\n");
	if (connect(sd, (struct sockaddr *)&sa, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr, "Impossibile connettersi: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	const int MAXLEN = 1024;
	char	  data[MAXLEN];
	ssize_t	  snd_bytes, rcvd_bytes;
	while (1) {
		// --- LETTURA LINEA DA STDIN --- //
		printf("Inserisci un messaggio da inviare al server: ");
		fgets(data, MAXLEN, stdin);
		int data_len = strlen(data);
		if (data[data_len - 1] == '\n') {
			data[data_len - 1] = '\0';
			data_len--;
		}

		// --- INVIO LUNGHEZZA DEL MESSAGGIO --- //
		// conversione a formato network (da big endian a little endian)
		int msg_len = htonl(data_len);
		snd_bytes   = send(sd, &msg_len, sizeof(int), 0);
		if (snd_bytes < 0) {
			fprintf(
			    stderr, "Impossibile inviare dati: %s\n", strerror(errno)
			);
			exit(EXIT_FAILURE);
		}
		printf("Inviati %ld bytes\n", snd_bytes);

		// --- INVIO MESSAGGIO --- //
		// manda il messaggio byte per byte
		size_t bytes_sent = 0;
		while (1) {
			char buff[1];
			buff[0]	  = data[bytes_sent];
			snd_bytes = send(sd, buff, 1, 0);
			if (snd_bytes < 0) {
				fprintf(
				    stderr,
				    "Impossibile inviare dati: %s\n",
				    strerror(errno)
				);
				exit(EXIT_FAILURE);
			}
			bytes_sent += snd_bytes;
			if (bytes_sent >= (size_t)data_len) {
				break;
			}
		}
		printf("Inviati %ld bytes\n", bytes_sent);

		// --- RICEZIONE RISPOSTA --- //
		char resp[3];
		rcvd_bytes = recv(sd, &resp, 2, 0);
		if (rcvd_bytes < 0) {
			fprintf(
			    stderr,
			    "Impossibile ricevere dati su socket: %s\n",
			    strerror(errno)
			);
			exit(EXIT_FAILURE);
		}
		resp[rcvd_bytes] = '\0';

		printf("Ricevuta risposta '%s'\n", resp);

		if (strcmp(data, "exit") == 0) {
			break;
		}
	}

	// --- CHIUSURA SOCKET --- //
	close(sd);

	return EXIT_SUCCESS;
}
