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

#include "common.h"

static const int OK = 1;
int				 sd = -1;

// chiude la connessione e termina il processo
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
int compress_folder(const char *dirname, const char *archive_path, char alg) {
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "tar -c -%c -f %s %s/*", alg, archive_path, dirname);

	printf(YELLOW("\tEsecuzione comando: '%s'\n"), cmd);
	if (system(cmd) != 0) {
		fprintf(stderr, MAGENTA("\tERRORE: Comando fallito\n"));
		return -1;
	}
	return 0;
}

// pulisce tutti i file nella cartella specificata
int clean_folder(const char *dirname, const char *what) {
	if (dirname == NULL || what == NULL) {
		return -1;
	}

	char  *command;
	size_t len = strlen(dirname) + strlen(what) + strlen("rm -rf    ") + 4;

	command = malloc(len);
	if (command == NULL) {
		return -1;
	}

	snprintf(command, len, "rm -rf %s/%s", dirname, what);
	system(command);
	free(command);

	return 0;
}

// gestisce la connessione con un solo client
int process_client(const char *myfolder) {
	char *cmd = NULL, *arg = NULL;

	while (1) {
		// evitiamo di introdurre memory leak :p, arg e cmd ci vengono inviati dal client,
		// le stringhe vengono allocate ogni volta, quindi le devo deallocare
		if (arg != NULL) {
			free(arg);
			arg = NULL;
		}
		if (cmd != NULL) {
			free(cmd);
			cmd = NULL;
		}

		// aspetta fino a quando non riceve un comando, ritorna il comando e l'argomento
		// se presente
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

		// se il client segnala il termine della connessione facciamo lo stesso
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

			// crea una cartella temporanea del processo, se esiste già bene
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
			// riceve il file
			e = receive_file(sd, filename);
			// torna nella cartella precedente
			chdir("..");

			// se il trasferimento del file non è andato a buon file segnalalo, se
			// l'errore era di connessione termina il processo
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

			char *archivename  = NULL;
			char *archive_path = NULL;

			int e = 0;

			// controllo che l'algoritmo sia valido
			if (alg[0] != 'z' && alg[0] != 'j') {
				fprintf(stderr, MAGENTA("\tERRORE: Algoritmo non conosciuto\n"));
				continue;
			}

			// calcola il nome e l'estensione dell'archivio, alloca memoria
			get_filename(alg[0], &archivename);

			// scrivi il percorso dell'archivio
			size_t archive_path_len;
			archive_path_len = strlen(archivename) + strlen(myfolder) + 4;
			archive_path	 = malloc(archive_path_len);
			if (archive_path == NULL) {
				fprintf(stderr, MAGENTA("\tERRORE: malloc(): %s\n"), strerror(errno));
				if (send_response(sd, !OK) < 0) {
					return -1;
				}
				continue;
			}
			snprintf(archive_path, archive_path_len, "%s/%s", myfolder, archivename);

			// comprime la cartella del processo, l'archivio finisce nella cartella del
			// processo
			e = compress_folder(myfolder, archive_path, alg[0]);

			// se la compressione non è andata a buon fine lo devo segnalare al client
			// così non rimane ad aspettare il file
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

			// invia l'archivio
			e = send_file(sd, archive_path);
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

			// una volta che l'archivio è stato inviato elimino tutti i file ricevuti, con
			// la prima chiamata a clean_folder elimino tutti i file normali
			if (clean_folder(myfolder, "*")) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Impossibile pulire la cartella del processo\n")
				);
			}
			// con la seconda chiamata elimino i file nascosti
			if (clean_folder(myfolder, ".*")) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Impossibile pulire la cartella del processo\n")
				);
			}

			// dealloco le stringhe che ho allocato nel frattempo
			free(archivename);
			free(archive_path);
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta
	//		argv[1] = porta

	// metto di default porta se argc < 2
	const char *addr_str = "0.0.0.0";
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

	// ignora la sigpipe, causa problemi su windows
	signal(SIGPIPE, SIG_IGN);

	printf(YELLOW("\tSocket %d associato a %s:%d\n"), listen_sd, addr_str, port_no);

	// --- LISTENING --- //
	// creo una coda di 10 connessioni max
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

		// aspetta le connessioni
		sd = accept(listen_sd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (sd < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile accettare connessione su socket: %s\n"),
				strerror(errno)
			);
			continue;
		}

		// conversione dell'indirizzo in formato numerico, serve per stampare l'indirizzo
		// del client
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

		// chiamo la fork per gestire la connessione, con processi diversi potrei gestire
		// diverse connessioni contemporaneamente (anche se non viene fatto atm)
		// inoltre uso il pid per generare il nome della cartella della sessione
		pid_t pid;
		pid = fork();

		// nome della cartella del figlio
		char childfolder[128];

		if (pid < 0) {
			// se ho avuto un errore nella fork chiudo tutto ed esco
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

			// registro il gestore di interrupt
			signal(SIGINT, quit);

			// genero il nome della cartella della sessione
			snprintf(childfolder, sizeof(childfolder), "folder-%d", getpid());

			// chiamo la funzione che gestisce la connessione con il client
			if (process_client(childfolder) < 0) {
				fprintf(stderr, RED("\tERRORE: Processo figlio uscito con errore\n"));
				exit(EXIT_FAILURE);
			}

			// arrivo qui quando il client manda "quit", tutti felici termino il processo
			// senza errori
			exit(EXIT_SUCCESS);
		} else {
			// processo genitore

			// anche il processo genitore deve conoscere il nome della cartella della
			// sessione per essere in grado di eliminarla
			snprintf(childfolder, sizeof(childfolder), "folder-%d", pid);
			printf(
				YELLOW("\tCreato processo figlio pid: %d, childfolder: '%s'\n"),
				pid,
				childfolder
			);

			// in questo momento supporto solo una connessione alla volta quindi aspetto
			// con pazienza che il processo figlio termini
			if (wait(NULL) < 0) {
				fprintf(
					stderr,
					RED("\tERRORE: Impossibile creare un processo figlio: %s\n"),
					strerror(errno)
				);
			}
			printf(YELLOW("\tIl processo %d ha terminato\n"), pid);

			// rimuovi la cartella del figlio, se esiste
			if (clean_folder(childfolder, "")) {
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
