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

#include "common.h"

// abbiamo deciso di mettere sd globale per poter fare la signal(SIGINT, quit);
int sd = -1;

// chiude il socket e termina il programma
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

// handler del segnale SIGINT, segnala al server che il client sta chiudendo la
// connessione e termina il programma
void quit_handler() {
	send_command(sd, "quit", NULL);
	quit();
}

// prende la stringa del comando e la divide in comando e argomento, se trova più di un
// comando ed un argomento ritorno con errore. Non alloca memoria
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

// tenta di connettere il socket globale al server con l'address specificato
int connect_to_server(struct sockaddr_in *sa) {
	if (connect(sd, (struct sockaddr *)sa, sizeof(struct sockaddr_in)) < 0) {
		fprintf(
			stderr, MAGENTA("\tERRORE: Impossibile connettersi: %s\n"), strerror(errno)
		);
		return -1;
	}
	return 0;
}

// stampa messaggi di aiuto, usato quando viene inserito un comando sbagliato
void help(void) {
	printf(
		"\tComandi disponibili:\n"
		"\thelp:\n\t --> mostra l'elenco dei comandi disponibili\n"
		"\tadd [file]\n\t --> invia il file specificato al server remoto\n"
		"\tcompress [alg]\n\t --> riceve dal server remoto "
		"l'archivio compresso secondo l'algoritmo specificato\n"
		"\tquit\n\t --> disconnessione\n\n"
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
		fprintf(stderr, RED("\tERRORE: Impossibile creare il socket\n"));
		exit(EXIT_FAILURE);
	}

	// connessione al server
	if (connect_to_server(&sa) < 0) {
		exit(EXIT_FAILURE);
	}

	// per rendere la chiusura con ^C piu' gentile, faccio fare la quit
	signal(SIGINT, quit_handler);
	// ignora la sigpipe, causa problemi su windows
	signal(SIGPIPE, SIG_IGN);

	// contatore della add, tiene conto del numero di file inviati
	int n_add = 0;

	// loop
	while (1) {
		const unsigned int CMD_MAX = 1024;
		char			   userinput[CMD_MAX];
		// stampa il prompt ed aspetta una riga di input dall'utente
		printf("rcomp> ");
		if (fgets(userinput, sizeof(userinput) - 1, stdin) == NULL) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile accettare comando: %s\n"),
				strerror(errno)
			);
			continue;
		}
		// rimuovi il newline dall'input
		size_t uin_len = strlen(userinput);
		if (uin_len == 0) {
			fprintf(stderr, MAGENTA("\tERRORE: Input utente di lunghezza zero\n"));
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
			fprintf(stderr, MAGENTA("\tERRORE: Troppi argomenti\n"));
			help();
			continue;
		}
		// gestione comandi
		// l'utente ha bisogno di aiuto
		if (strcmp(cmd, "help") == 0) {
			help();
		} else if (strcmp(cmd, "quit") == 0) {
			send_command(sd, "quit", NULL);
			quit();
			break;
		} else if (strcmp(cmd, "compress") == 0) {
			// controlla che sia stato inviato almeno un file
			if (n_add < 1) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: aggiungere almeno un file "
							"prima di effettuare la compressione\n")
				);
				continue;
			}
			// bisogna leggere arg, se non e' NULL allora lo cambio nel valore di default
			// che è compressione in gzip
			if (arg == NULL) {
				arg = "z";
			}

			// controllo che l'algoritmo di compressione sia valido
			if (strcmp(arg, "z") != 0 && strcmp(arg, "j") != 0) {
				fprintf(stderr, MAGENTA("\tERRORE: Campo [alg] non valido\n"));
				continue;
			}

			// invio il comando al server
			if (send_command(sd, "compress", arg) < 0) {
				fprintf(stderr, MAGENTA("\tERRORE: Impossibile mandare il comando\n"));
				if (is_network_error(errno)) {
					quit();
				}
				continue;
			}

			// aspetto un riscontro dal server, il server può fallire quando tar fallisce
			// per qualche motivo
			if (receive_response(sd) < 0) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Il server ha fallito nel comprimere i file\n")
				);
				if (is_network_error(errno)) {
					quit();
				}
				continue;
			}

			// il nome dell'archivio è di default "archivio_compresso.tar" bisogna
			// aggiungerci il suffisso corretto ".gz" o ".bz2"
			char *path;
			// mette l'estensione al nome del file a seconda dell'algoritmo
			get_filename(arg[0], &path);
			// ora ho dove voglio creare il file da ricevere, quello compresso
			if (receive_file(sd, path) < 0) {
				fprintf(stderr, MAGENTA("\tERRORE: Ricezione del file fallita\n"));
				if (is_network_error(errno)) {
					quit();
				}
			}
			// get_filename alloca memoria
			free(path);
			// resetta il contatore dei file, la sessione è pulita
			n_add = 0;
		} else if (strcmp(cmd, "add") == 0) {
			if (arg == NULL) {
				fprintf(stderr, MAGENTA("\tERRORE: Campo [file] mancante\n"));
				continue;
			}
			// mette in file il secondo argomento, ovvero il path
			char *filename = arg;
			// verifica che il nome del file sia composto solamente
			// da [a-z]|[A-Z]|[0-9]|(.)
			// NOTA: accetta file con path relativo o assoluto
			char *foff	   = strrchr(filename, '/');
			if (foff != NULL) {
				filename = foff + 1;
			}

			int	   invalid_name = 0;
			size_t fname_len	= strlen(filename);
			for (size_t i = 0; i < fname_len; i++) {
				if (!isascii(filename[i]) && !isalpha(filename[i]) &&
					!isdigit(filename[i]) && filename[i] != '.') {
					invalid_name = 1;
					break;
				}
			}

			// se il nome del file è invalido, segnalalo e ignora il comando
			if (invalid_name) {
				fprintf(
					stderr, MAGENTA("\tERRORE: Nome file '%s' non valido\n"), filename
				);
				continue;
			}
			// controllo per evitare di fare la send su un file che non esiste
			FILE *exists = fopen(arg, "rb");
			if (exists == NULL) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: impossibile aggiungere file (%s)\n"),
					strerror(errno)
				);
				continue;
			} else {
				fclose(exists);
			}

			// invial il comando "add", segnala al server che sto per inviare un file con
			// il nome specificato
			if (send_command(sd, "add", filename) < 0) {
				fprintf(stderr, MAGENTA("\tERRORE: Impossibile mandare il comando\n"));
				if (is_network_error(errno)) {
					quit();
				}
				continue;
			}

			// invio il file
			if (send_file(sd, arg) < 0) {
				fprintf(
					stderr,
					MAGENTA("\tERRORE: Errore nel trasferimento del file %s\n"),
					filename
				);
				if (is_network_error(errno)) {
					quit();
				}
				continue;
			}

			// incremento il contatore dei file inviati
			n_add++;
		} else {
			// se non ho riconosciuto il comando allora stampo l'aiuto
			printf(MAGENTA("\tERRORE: Comando non riconosciuto\n"));
			help();
		}
	}

	return 0;
}
