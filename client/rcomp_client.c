#define _XOPEN_SOURCE	500
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <ctype.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "../common.h"

// abbiamo deciso di mettere sd globale per poter fare la signal(SIGINT, quit);
int sd = -1;

void quit() {
	if (close(sd) < 0) {
		fprintf(stderr, "Impossibile chiudere il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	printf("Chiusura del socket avvenuta con successo\n");
	exit(EXIT_SUCCESS);
}

int read_command(char *str, char **com, char **arg) {
	char *command = NULL, *argument = NULL, *tmp;
	int	  argc = 0;

	// divido la stringa in token divisi da spazi, salvo il primo come comando
	// e il secondo come argomento
	for (char *s = str; (tmp = strtok(s, " ")) != NULL; argc++, s = NULL) {
		if (argc == 0) {
			command = tmp;
		} else if (argc == 1) {
			argument = tmp;
		} else {
			// se ne sta trovando piu' di 2
			return -1;
		}
	}

	*com = command;
	*arg = argument;
	// ritorno il numero di argomenti trovati
	return argc;
}

int connect_to_server(struct sockaddr_in *sa) {
	if (connect(sd, (struct sockaddr *)sa, sizeof(struct sockaddr_in)) < 0) {
		fprintf(stderr, "Impossibile connettersi: %s\n", strerror(errno));
		return -1;
	}
	return 0;
}

void help(void) {
	printf(
		"Comandi disponibili:\n"
		"help:\n --> mostra l'elenco dei comandi disponibili\n"
		"add [file]\n --> invia il file specificato al server remoto\n"
		"compress [alg]\n --> riceve dal server remoto "
		"l'archivio compresso secondo l'algoritmo specificato\n"
		"quit\n --> disconnessione\n\n"
	);
}

int main(int argc, char *argv[]) {
	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta
	//		argv[1] = indirizzo
	//		argv[2] = porta
	// metto di default porta e indirizzo se argc < 3

	// prendi l'indirizzo e la porta da riga di comando
	const char *addr_str = "127.0.0.1";
	int			port_no	 = 1234;
	if (argc == 3) {
		addr_str = argv[1];	 // oppure strdup
		port_no	 = atoi(argv[2]);
	}
	// creazione del socket
	struct sockaddr_in sa;
	if (socket_stream(addr_str, port_no, &sd, &sa) < 0) {
		fprintf(stderr, "Impossibile creare il socket\n");
		exit(EXIT_FAILURE);
	}

	if (connect_to_server(&sa) < 0) {
		exit(EXIT_FAILURE);
	}

	// per rendere la chiusura con ^C piu' gentile, faccio fare la quit
	signal(SIGINT, quit);
	// loop
	while (1) {
		const unsigned int CMD_MAX = 1024;
		char			   userinput[CMD_MAX];
		printf("rcomp> ");
		if (fgets(userinput, sizeof(userinput) - 1, stdin) == NULL) {
			fprintf(stderr, "Impossibile accettare comando: %s\n", strerror(errno));
			continue;
		}
		// rimuovi il newline dall'input
		size_t uin_len = strlen(userinput);
		if (uin_len == 0) {
			fprintf(stderr, "Input utente di lunghezza zero\n");
			continue;
		}
		if (userinput[uin_len - 1] == '\n') {
			userinput[uin_len - 1] = '\0';
		}

		// dividi il comando in sotto stringhe divise da ' '
		// "compress b" <- secondo argomento
		//  ^^^^^^^^
		//  primo argomento
		char *cmd;
		char *arg;
		if (read_command(userinput, &cmd, &arg) < 0) {
			fprintf(stderr, "Troppi argomenti\n");
			help();
			continue;
		}
		// gestione comandi
		if (strcmp(cmd, "help") == 0) {
			help();
		} else if (strcmp(cmd, "quit") == 0) {
			send_command("quit", NULL);
			close(sd);	// quit del client
			exit(EXIT_SUCCESS);
		} else if (strcmp(cmd, "compress") == 0) {
			// bisogna leggere arg, se non e' NULL allora lo cambio
			if (arg == NULL) {
				arg = "z";
			}

			if (strcmp(arg, "z") != 0 && strcmp(arg, "j") != 0) {
				fprintf(stderr, "Campo [alg] non valido\n");
				continue;
			}

			send_command("compress", arg);

			if (receive_response() < 0) {
				fprintf(stderr, "Il server ha fallito nel comprimere i file\n");
				continue;
			}

			char *path;
			// mette l'estensione al nome del file a seconda dell'algoritmo
			get_filename(arg[0], &path);
			// ora ho dove voglio creare il file da ricevere, quello compresso
			receive_file(path);
			free(path);
		} else if (strcmp(cmd, "add") == 0) {
			if (arg == NULL) {
				fprintf(stderr, "Campo [file] mancante\n");
				continue;
			}
			char *filename = arg;
			// mette in file il secondo argomento, ovvero il path
			// verifica che il nome del file sia composto solamente
			// da [a-z]|[A-Z]|[0-9]|(.)
			// NOTA: accetta file con path relativo o assoluto
			char *foff	   = strrchr(filename, '/');
			if (foff != NULL) {
				filename = foff + 1;
			}
			int invalid_name = 0;
			for (size_t i = 0; i < strlen(filename); i++) {
				if (!isascii(filename[i]) && !isalpha(filename[i]) &&
					!isdigit(filename[i]) && filename[i] != '.') {
					invalid_name = 1;
					break;
				}
			}

			if (invalid_name) {
				fprintf(stderr, "Nome file '%s' non valido\n", filename);
				continue;
			}

			send_command("add", filename);
			if (send_file(arg) < 0) {
				fprintf(stderr, "Errore nel trasferimento del file %s\n", filename);
				return -1;
			}
		} else {
			printf("Comnado non riconosciuto\n");
			help();
		}
	}

	return 0;
}