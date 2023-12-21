#define _XOPEN_SOURCE	500
#define _POSIX_C_SOURCE 200809L

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
#include <sys/wait.h>
#include <arpa/inet.h>

#include "../common.h"

static const int OK = 1;
int				 sd = -1;

void quit() {
	if (close(sd) < 0) {
		fprintf(
			stderr, RED("\tERRORE: Impossibile chiudere il socket: %s\n"), strerror(errno)
		);
		exit(EXIT_FAILURE);
	}
	printf(YELLOW("\tChiusura del socket avvenuta con successo\n"
	) "\tDisconnessione effettuata\n");
	exit(EXIT_SUCCESS);
}

// attraverso la funzione "system()" comprime la cartella dei file del client
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(const char *dirname, const char *archivename, char alg) {
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "tar -c -%c -f %s %s", alg, archivename, dirname);

	printf(YELLOW("\tEsecuzione comando: '%s'\n"), cmd);
	if (system(cmd) != 0) {
		fprintf(stderr, MAGENTA("\tERRORE: Comando fallito\n"));
		return -1;
	}
	return 0;
}

// gestisce il socket di un client
int process_client(const char *myfolder) {
	char *cmd = NULL, *arg = NULL;

	while (1) {
		// evitiamo di introdurre memory leak :p
		if (arg != NULL) {
			free(arg);
		}
		if (cmd != NULL) {
			free(cmd);
		}

		if (receive_command(sd, &cmd, &arg) < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile ricevere il comando: %s\n"),
				strerror(errno)
			);
			if (is_network_error(errno)) {
				quit();
			}
			continue;
		}

		if (strcmp(cmd, "quit") == 0) {
			send_response(sd, OK);
			quit();
			printf("\tClient disconnesso\n");
			break;
		} else if (strcmp(cmd, "add") == 0) {
			// controllo di avere il nome del file
			char *filename = arg;
			if (filename == NULL) {
				fprintf(
					stderr, MAGENTA("\tERRORE: Ricevuto il comando add senza file\n")
				);
				continue;
			}

			// crea una cartella temporanea del processo
			int e = mkdir(myfolder, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
			if (e < 0 && errno != EEXIST) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Impossibile creare la cartella di processo: %s\n"),
					strerror(errno)
				);
				return -1;
			}
			// entra nella cartella
			chdir(myfolder);
			e = receive_file(sd, filename);
			chdir("..");

			if (e < 0) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Impossibile ricevere il file %s\n"),
					filename
				);
				if (is_network_error(errno)) {
					quit();
				}
				continue;
			}

		} else if (strcmp(cmd, "compress") == 0) {
			// controllo di avere il nome del file
			char *alg = arg;
			if (alg == NULL) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Ricevuto il comando compress senza algoritmo\n")
				);
				continue;
			}

			char *archivename;
			int	  e = 0;

			if (alg[0] != 'z' && alg[0] != 'j') {
				fprintf(stderr, MAGENTA("\tERRORE: Algoritmo non conosciuto\n"));
				continue;
			}

			get_filename(alg[0], &archivename);
			e = compress_folder(myfolder, archivename, alg[0]);

			if (e < 0) {
				fprintf(
					stderr, MAGENTA("\tERRORE: Impossibile comprimere %s\n"), myfolder
				);
				if (send_response(sd, !OK) < 0) {
					return -1;
				}
				continue;
			}
			if (send_response(sd, OK) < 0) {
				return -1;
			}

			e = send_file(sd, archivename);
			if (e < 0) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Impossibile inviare l'archivio %s\n"),
					archivename
				);
				if (is_network_error(errno)) {
					quit();
				}
			}
			free(archivename);
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta
	//		argv[1] = porta

	// metto di default porta se argc < 2
	const char *addr_str = "127.0.0.1";
	int			port_no	 = 1234;
	// prendi l'indirizzo e la porta da riga di comando
	if (argc == 2) {
		port_no = atoi(argv[1]);
	}
	// --- CREAZIONE SOCKET --- //
	int				   listen_sd;
	struct sockaddr_in sa;
	if (socket_stream(addr_str, port_no, &listen_sd, &sa) < 0) {
		fprintf(
			stderr, RED("\tERRORE: Impossibile creare il socket: %s\n"), strerror(errno)
		);
		exit(EXIT_FAILURE);
	}
	// --- BINDING --- //

	// associazione indirizzo a socket
	if (bind(listen_sd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(
			stderr,
			RED("\tERRORE: Impossibile associare l'indirizzo a un socket: %s\n"),
			strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	// ignora la sigpipe
	signal(SIGPIPE, SIG_IGN);

	printf(YELLOW("\tSocket %d associato a %s:%d\n"), listen_sd, addr_str, port_no);

	// --- LISTENING --- //
	if (listen(listen_sd, 10) < 0) {
		fprintf(
			stderr,
			RED("\tERRORE: Impossibile mettersi in attesa su socket: %s\n"),
			strerror(errno)
		);
		exit(EXIT_FAILURE);
	}
	struct sockaddr_in client_addr;
	char			   client_addr_str[INET_ADDRSTRLEN];

	while (1) {
		// --- ATTESA DI CONNESSIONE --- //
		printf("\tIn attesa di connessione sulla porta %d\n", port_no);
		socklen_t client_addr_len = sizeof(client_addr);
		sd = accept(listen_sd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (sd < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile accettare connessione su socket: %s\n"),
				strerror(errno)
			);
			continue;
		}

		// conversione dell'indirizzo in formato numerico
		const char *res = inet_ntop(
			AF_INET, &client_addr.sin_addr.s_addr, client_addr_str, INET_ADDRSTRLEN
		);
		if (res == NULL) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile convertire l'indirizzo: %s\n"),
				strerror(errno)
			);
		} else {
			printf(
				("\tConnessione stabilita con il client %s:%d\n"),
				addr_str,
				ntohs(client_addr.sin_port)
			);
		}

		pid_t pid;
		pid = fork();

		// nome della cartella del figlio
		char childfolder[64];

		if (pid < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile creare un processo figlio: %s\n"),
				strerror(errno)
			);
			close(sd);
			close(listen_sd);
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			// processo figlio
			signal(SIGINT, quit);
			snprintf(childfolder, sizeof(childfolder), "folder-%d", getpid());
			if (process_client(childfolder) < 0) {
				fprintf(stderr, RED("\tERRORE: Processo figlio uscito con errore\n"));
				exit(EXIT_FAILURE);
			}

			exit(EXIT_SUCCESS);
		} else {
			// processo genitore
			snprintf(childfolder, sizeof(childfolder), "folder-%d", pid);
			printf(
				YELLOW("\tCreato processo figlio pid: %d, childfolder: '%s'\n"),
				pid,
				childfolder
			);
			if (wait(NULL) < 0) {
				fprintf(
					stderr,
					RED("\tERRORE: Impossibile creare un processo figlio: %s\n"),
					strerror(errno)
				);
			}
			printf(YELLOW("\tIl processo %d ha terminato\n"), pid);
			// rimuovi la cartella del figlio se esiste
			char rm_command[sizeof("rm -rf ") + sizeof(childfolder) + 1];
			snprintf(rm_command, sizeof(rm_command), "rm -rf %s", childfolder);
			if (system(rm_command)) {
				fprintf(
					stderr,
					RED("\tERRORE: Impossibile eliminare la cartella del figlio\n")
				);
			}
		}
	}

	// --- CHIUSURA SOCKET --- //
	// FIXME: qui non ci arrivo mai, che devo fare?
	close(listen_sd);

	return EXIT_SUCCESS;
}
