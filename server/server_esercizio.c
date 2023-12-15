/*
 * server_v3.c
 *
 * Il programma server:
 * - apre un socket di tipo stream
 * - attende la connessione da parte di un client
 * - quando un client si connette, crea un processo figlio che gestisce
 *   la connessione col client.
 * --- il processo figlio, ciclicamente, riceve un messaggio dal client
 *     e invia una risposta. Quando viene ricevuta la stringa 'exit',
 *     termina
 * --- il processo genitore attende una nuova connessione da un client
 */

#include <arpa/inet.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

int process_client(int conn_sd) {
	ssize_t rcvd_bytes, snd_bytes;
	while (1) {
		// --- RICEZIONE LUNGHEZZA DELLA STRINGA --- //
		int data_len;
		rcvd_bytes = recv(conn_sd, &data_len, sizeof(int), 0);
		if (rcvd_bytes < 0) {
			fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		data_len = ntohl(data_len);	 // conversione a formato host
		printf("Lunghezza stringa '%d'\n", data_len);

		char *data = (char *)malloc(data_len + 1);

		// --- RICEZIONE DATI --- //
		// ricevi byte per byte
		size_t bytes_received = 0;
		while (1) {
			char buff[1];
			rcvd_bytes = recv(conn_sd, buff, 1, 0);
			if (rcvd_bytes < 0) {
				fprintf(
					stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno)
				);
				exit(EXIT_FAILURE);
			}
			data[bytes_received] = buff[0];
			bytes_received += rcvd_bytes;
			if (bytes_received >= (size_t)data_len) {
				break;
			}
		}
		data[bytes_received] = '\0';

		printf("Ricevuto %ld '%s'\n", bytes_received, data);

		// --- INVIO RISPOSTA --- //
		snd_bytes = send(conn_sd, "OK", 2, 0);
		if (snd_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati su socket: %s\n", strerror(errno));
			exit(EXIT_FAILURE);
		}
		printf("Risposta inviata\n");

		if (strcmp(data, "exit") == 0) {
			free(data);
			break;
		}

		free(data);
	}

	// --- CHIUSURA SOCKET CONNESSO --- //
	close(conn_sd);
	return 0;
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	char *addr_str = "127.0.0.1";
	int	  port_no  = 10000;

	// --- CREAZIONE SOCKET --- //
	int sd;
	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
		fprintf(stderr, "Impossibile creare il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// --- BINDING --- //

	// conversione dell'indirizzo in formato numerico
	in_addr_t address;
	if (inet_pton(AF_INET, addr_str, (void *)&address) < 0) {
		fprintf(stderr, "Impossibile convertire l'indirizzo: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// preparazione della struttura contenente indirizzo IP e porta
	struct sockaddr_in sa;
	sa.sin_family	   = AF_INET;
	sa.sin_port		   = htons(port_no);
	sa.sin_addr.s_addr = address;

	// DEBUG
	int reuse_opt = 1;
	setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &reuse_opt, sizeof(int));

	// associazione indirizzo a socket
	if (bind(sd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(
			stderr, "Impossibile associare l'indirizzo a un socket: %s\n", strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	printf("Socket %d associato a %s:%d\n", sd, addr_str, port_no);

	// --- LISTENING --- //
	if (listen(sd, 10) < 0) {
		fprintf(
			stderr, "Impossibile mettersi in attesa su socket: %s\n", strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	// --- ATTESA DI CONNESSIONE --- //
	printf("--- In attesa di connessione ---\n");

	int				   conn_sd;
	struct sockaddr_in client_addr;
	char			   client_addr_str[INET_ADDRSTRLEN];

	while (1) {
		socklen_t client_addr_len = sizeof(client_addr);
		conn_sd = accept(sd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (conn_sd < 0) {
			fprintf(
				stderr,
				"Impossibile accettare connessione su socket: %s\n",
				strerror(errno)
			);
			exit(EXIT_FAILURE);
			// continue;
		}

		// conversione dell'indirizzo in formato numerico
		const char *res = inet_ntop(
			AF_INET, &client_addr.sin_addr.s_addr, client_addr_str, INET_ADDRSTRLEN
		);
		if (res == NULL) {
			fprintf(stderr, "Impossibile convertire l'indirizzo: %s\n", strerror(errno));
		} else {
			printf("Connesso col client %s:%d\n", addr_str, ntohs(client_addr.sin_port));
		}

		pid_t pid;
		if ((pid = fork()) < 0) {
			fprintf(
				stderr, "Impossibile creare un processo figlio: %s\n", strerror(errno)
			);
			close(conn_sd);
			close(sd);
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			// processo figlio
			process_client(conn_sd);
			exit(EXIT_SUCCESS);
		} else {
			// processo genitore
			if (wait(NULL) < 0) {
				fprintf(
					stderr, "Impossibile creare un processo figlio: %s\n", strerror(errno)
				);
			}
			printf("Il processo %d ha terminato", pid);
		}
	}

	// --- CHIUSURA SOCKET --- //
	close(sd);

	return EXIT_SUCCESS;
}
