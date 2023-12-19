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
	if (close(sd) < 0) {
		fprintf(stderr, "Impossibile chiudere il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	printf("Chiusura del socket avvenuta con successo\n");
	exit(EXIT_SUCCESS);
}

// attraverso la funzione "system()" comprime la cartella dei file del client
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(const char *dirname, char alg) {
	char cmd[128];

	if (alg == 'z') {
		snprintf(cmd, sizeof(cmd), "tar -c -z -f archivio_compresso.tar.gz %s", dirname);
	} else if (alg == 'j') {
		snprintf(cmd, sizeof(cmd), "tar -c -j -f archivio_compresso.tar.bz2 %s", dirname);
	} else {
		printf("Algoritmo non valido\n");
		return -1;
	}
	if (system(cmd) != 0) {
		fprintf(stderr, "Impossibile fare la system() %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

// gestisce il socket di un client
int client_process(void) {
	char *cmd = NULL, *arg = NULL;

	// nome della mia personalissima cartella
	char myfolder[64];
	snprintf(myfolder, sizeof(myfolder), "folder-%d", getpid());

	while (1) {
		if (receive_command(&cmd, &arg) < 0) {
			fprintf(stderr, "Impossibile ricevere il comando\n");
			continue;
		}

		if (strcmp(cmd, "quit")) {
			send_response(ok);
			// rimuovi la cartella
			char command[sizeof("rm -rf ") + sizeof(myfolder)];
			snprintf(command, sizeof(command), "rm -rf %s", myfolder);
			system(command);
			break;
		} else if (strcmp(cmd, "add")) {
			// controllo di avere il nome del file
			char *filename = arg;
			if (filename == NULL) {
				fprintf(stderr, "Ricevuto il comando add senza file\n");
				goto free_args;
			}

			// crea una cartella temporanea del processo
			int e = mkdir(myfolder, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
			if (e < 0 && errno != EEXIST) {
				fprintf(
					stderr,
					"Impossibile creare la cartella di processo: %s\n",
					strerror(errno)
				);
				return -1;
			}
			// entra nella cartella
			chdir(myfolder);
			e = receive_file(filename);
			chdir("..");

			if (e < 0) {
				fprintf(stderr, "Impossibile ricevere il file %s\n", filename);
				goto free_args;
			}

		} else if (strcmp(cmd, "compress")) {
			// controllo di avere il nome del file
			char *alg = arg;
			if (alg == NULL) {
				fprintf(stderr, "Ricevuto il comando compress senza file\n");
				goto free_args;
			}

			if (compress_folder(myfolder, alg[0]) < 0) {
				goto free_args;
			}
		}

	// brutte cose a me
	free_args:
		// evitiamo di introdurre memory leak :p
		if (arg) {
			free(arg);
		}
		if (cmd) {
			free(cmd);
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
	struct sockaddr_in *sa;
	if (socket_stream(&addr_str, port_no, &sa) < 0) {
		printf(stderr, "Impossibile creare il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	// --- BINDING --- //

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
			/* qua il padre pulisce per il figlio la directory e il file */
		}
	}

	// --- CHIUSURA SOCKET --- //
	close(sd);

	return EXIT_SUCCESS;
}
