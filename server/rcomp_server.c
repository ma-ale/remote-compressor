#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../common.h"

const int ok = 1;
int		  sd = -1;

void quit() {
	;
	if (close(listen_sd) < 0) {
		fprintf(stderr, "Impossibile chiudere il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	printf("Chiusura del socket avvenuta con successo\n");
	exit(EXIT_SUCCESS);
}

// attraverso la funzione "system()" comprime la cartella dei file del client
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(char alg) {
	char command[1024];
	char dirname[64];
	snprintf(dirname, 64, "%d", getpid());

	if (alg == 'z') {
		snprintf(command, 1024, "tar -c -z -f archivio_compresso.tar.gz %s", dirname);
	} else if (alg == 'j') {
		snprintf(command, 1024, "tar -c -j -f archivio_compresso.tar.bz2 %s", dirname);
	} else {
		printf("Algoritmo non valido\n");
		return -1;
	}
	if (system(command) != 0) {
		fprintf(stderr, "Impossibile fare la system() %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

// gestisce il socket di un client
int client_process(void) {
	while (1) {
		char *cmd, *arg;
		if (receive_command(&cmd, &arg) < 0) {
			fprintf(stderr, "Impossibile ricevere il comando\n");
			continue;
		}

		if (strcmp(cmd, "quit")) {
			send_response(ok);
			// rimuovi la cartella
			// visto che pid e' al massimo un numero di 5 caratteri, creo un
			// buffer di un ordine di grandezza maggiore: 64
			// 64 eè piu' bello di 32 e 16 :)
			char command[sizeof("rm -rf ") + 64];
			snprintf(command, sizeof("rm -rf ") + 64, "rm -rf %d", getpid());
			system(command);
			break;
		} else if (strcmp(cmd, "add")) {
			// controllo di avere il nome del file
			char *filename = arg;
			if (filename == NULL) {
				fprintf(stderr, "Ricevuto il comando add senza file\n");
				continue;
			}

			if (receive_file(filename) < 0) {
				fprintf(stderr, "Impossibile ricevere il file %s\n", filename);
				continue;
			}

			continue;
		} else if (strcmp(cmd, "compress")) {
			// controllo di avere il nome del file
			char *alg = arg;
			if (alg == NULL) {
				fprintf(stderr, "Ricevuto il comando compress senza file\n");
				continue;
			}

			if (compress_folder(alg[0]) < 0) {
				continue;
			}

			continue;
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta
	//		argv[1] = porta
	(void)argc;
	(void)argv;
	// metto di default porta se argc < 2
	const char *addr_str = "127.0.0.1";
	int			port_no	 = 1234;
	// prendi l'indirizzo e la porta da riga di comando
	if (argc == 2) {
		port_no = atoi(argv[1]);
	}
	// --- CREAZIONE SOCKET --- //
	int					listen_sd;
	struct sockaddr_in *sa;
	if (socket_stream(&addr_str, port_no, &listen_sd, &sa) < 0) {
		printf(stderr, "Impossibile creare il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	// --- BINDING --- //

	// associazione indirizzo a socket
	if (bind(listen_sd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(
			stderr, "Impossibile associare l'indirizzo a un socket: %s\n", strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	printf("Socket %d associato a %s:%d\n", listen_sd, addr_str, port_no);

	// --- LISTENING --- //
	if (listen(listen_sd, 10) < 0) {
		fprintf(
			stderr, "Impossibile mettersi in attesa su socket: %s\n", strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	// --- ATTESA DI CONNESSIONE --- //
	printf("--- In attesa di connessione ---\n");

	struct sockaddr_in client_addr;
	char			   client_addr_str[INET_ADDRSTRLEN];

	while (1) {
		socklen_t client_addr_len = sizeof(client_addr);
		sd = accept(listen_sd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (sd < 0) {
			fprintf(
				stderr,
				"Impossibile accettare connessione su socket: %s\n",
				strerror(errno)
			);
			continue;
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
			close(sd);
			close(listen_sd);
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			// processo figlio
			process_client(sd);
			exit(EXIT_SUCCESS);
		} else {
			// processo genitore
			if (wait(NULL) < 0) {
				fprintf(
					stderr, "Impossibile creare un processo figlio: %s\n", strerror(errno)
				);
			}
			printf("Il processo %d ha terminato", pid);
			/* qua il padre pulisce per il figlio la directory e il file */
		}
	}

	// --- CHIUSURA SOCKET --- //
	close(listen_sd);

	return EXIT_SUCCESS;
}
