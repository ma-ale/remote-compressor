#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>

int read_command(char *str, const char **com, const char **arg) {
	char  *command = NULL, *argument = NULL, *tmp;
	char **saveptr;
	int	   argc = 0;

	// divido la stringa in token divisi da spazi, salvo il primo come comando
	// e il secondo come argomento
	for (; (tmp = strtok_r(str, " ", saveptr)) != NULL; argc++) {
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

ssize_t file_dimension(const char *path) {
	// recupero dei metadati del file
	struct stat file_stat;
	if (stat(path, &file_stat) < 0) {
		fprintf(stderr, "Errore nella lettura delle informazioni del file %s\n", path);
		return -1;
	}

	// se il file e' un file regolare, visualizza la sua dimensione
	ssize_t file_size;
	if (S_ISREG(file_stat.st_mode) > 0) {
		file_size = file_stat.st_size;
	} else {
		printf("Il file non e' un file regolare\n");
		return -1;
	}
	return file_size;
}

int send_file(int sd, const char *path) {
	ssize_t file_dim = file_dimension(path);
	if (file_dim < 0) {
		// l'errore specifico viene stampato da file_dimension()
		// il server si aspetta un file quindi gli devo mandare qualcosa
		// TODO: cosa gli dico al server?
		return -1;
	}

	// se il file è troppo grande non gli posso mandare la dimensione
	if (file_dim > UINT_MAX) {
		// TODO: cosa gli dico al server?
		fprintf(
			stderr,
			"Il file è troppo grande: %.2fGiB\n",
			(float)file_dim / (float)(1024 * 1024 * 1024)
		);
		return -1;
	}

	uint32_t msg_len = htonl(file_dim);

	size_t sent_bytes = send(sd, &msg_len, sizeof(uint32_t), 0);
	if (sent_bytes < 0) {
		fprintf(stderr, "Impossibile inviare dati: %s\n", strerror(errno));
		return -1;
	}

	FILE *file = fopen(path, "r");

	// manda il file byte per byte
	size_t bytes_sent = 0;
	while (1) {
		char buff[1];
		int	 c = fgetc(file);
		if (c == EOF) {
			break;
		}
		buff[0]	   = c;
		sent_bytes = send(sd, buff, 1, 0);
		if (sent_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati: %s\n", strerror(errno));
			return -1;
		}
		bytes_sent += sent_bytes;
		if (bytes_sent >= (size_t)file_dim) {
			break;
		}
	}
	return 0;
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
	ssize_t rcvd_bytes;
	char	resp[3];
	rcvd_bytes = recv(sd, &resp, 2, 0);
	if (rcvd_bytes < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return (-1);
	}
	resp[rcvd_bytes] = '\0';
	if (strcmp(resp, "OK") != 0) {
		fprintf(stderr, "Server segnala il seguente errore: %s\n", resp);
		return (-1);
	}
	printf("Ricevuto l'OK dal server/n");

	return 0;
}

// path specifica il nome ed il percorso del file
int receive_file(int sd, char ext) {
	char path[64] = "archivio_compresso.tar";
	if (ext == 'z') {
		strcat(path, ".gz");
	} else if (ext == 'j') {
		strcat(path, ".bz2");
	} else {
		fprintf(stderr, "Estensione file non riconosciuta: %c\n", ext);
		return -1;
	}

	// apri o crea il file specificato da path in scrittura, troncato a zero
	FILE *file = fopen(path, "w+");
	if (file == NULL) {
		fprintf(stderr, "Impossibile aprire il file: %s\n", strerror(errno));
		return -1;
	}

	// --- RICEZIONE LUNGHEZZA FILE --- //
	int msg_len = 0, file_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return -1;
	}
	file_dim = ntohl(msg_len);
	printf("Ricevuti %d byte di lunghezza del file\n", file_dim);

	// --- RICEZIONE FILE --- //
	char	buff[1];
	ssize_t rcvd_bytes = 0;
	ssize_t recv_tot   = 0;
	while (recv_tot < (ssize_t)file_dim) {
		if ((rcvd_bytes = recv(sd, &buff, 1, 0)) < 0) {
			fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
			fclose(file);
			remove(path);
			return -1;
		}
		fputc(buff[0], file);
		recv_tot += rcvd_bytes;
	}

	fclose(file);
	if (recv_tot != (ssize_t)file_dim) {
		printf(
			"Ricezione del file fallita: ricevuti %ld byte in piu' della dimensione "
			"del file\n",
			(recv_tot - file_dim)
		);
		remove(path);
		return -1;
	} else {
		printf("Ricevuti %ld byte di file\n", recv_tot);
	}

	return 0;
}

// manda un comando testuale al server come "quit" e "compress"
// sd: descriptor del socket
// str: stringa che contiene il comando
// esempio: send_command(sd, "exit");
int send_command(int sd, const char *com, const char *arg) {
	// --- INVIO LUNGHEZZA --- //
	// conversione a formato network (da big endian a little endian)
	size_t com_len	  = strlen(com);
	int	   msg_len	  = htonl(com_len);
	size_t sent_bytes = send(sd, &msg_len, sizeof(int), 0);
	if (sent_bytes < 0) {
		fprintf(stderr, "Impossibile inviare dati comando: %s\n", strerror(errno));
		return (-1);
	}
	printf("Inviati %ld bytes di lunghezza comando\n", sent_bytes);
	// --- INVIO COMANDO --- //
	// manda il comando byte per byte
	size_t bytes_sent = 0;
	char   buff[1];
	while (1) {
		buff[0]	   = com[bytes_sent];
		sent_bytes = send(sd, buff, 1, 0);
		if (sent_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati comando: %s\n", strerror(errno));
			return (-1);
		}
		bytes_sent += sent_bytes;
		if (bytes_sent == (size_t)com_len) {
			break;
		} else if (bytes_sent > (size_t)com_len) {
			printf(
				"Invio comando fallito: inviati %ld byte in piu' della dimensione del "
				"comando\n",
				(bytes_sent - com_len)
			);
			return (-1);
		}
	}
	printf("Inviati %ld bytes di comando\n", bytes_sent);

	if (arg == NULL) {
		printf("Il comando non prevede argomento: inviati 0 byte di argomento");
		return 0;
	}
	// --- INVIO LUNGHEZZA ARGOMENTO--- //
	// conversione a formato network (da big endian a little endian)
	size_t arg_len = strlen(arg);
	msg_len		   = htonl(arg_len);
	sent_bytes	   = send(sd, &msg_len, sizeof(int), 0);
	if (sent_bytes < 0) {
		fprintf(stderr, "Impossibile inviare dati argomento: %s\n", strerror(errno));
		return (-1);
	}
	printf("Inviati %ld bytes di lunghezza argomento\n", sent_bytes);
	// --- INVIO ARGOMENTO --- //

	// manda l'argomento byte per byte
	sent_tot = 0;
	while (1) {
		buff[0]	   = arg[sent_tot];
		sent_bytes = send(sd, buff, 1, 0);
		if (sent_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati argomento: %s\n", strerror(errno));
			return (-1);
		}
		sent_tot += sent_bytes;
		if (sent_tot == (size_t)arg_len) {
			break;
		} else if (sent_tot > (size_t)arg_len) {
			printf(
				"Invio argomento fallito: inviati %ld byte in piu' della dimensione "
				"dell' argomento\n",
				(sent_tot - arg_len)
			);
			return (-1);
		}
	}
	printf("Inviati %ld bytes di argomento\n", sent_tot);

	return 0;
}

void help(void) {
	printf(
		"Comandi disponibili:\n"
		"help:\n --> mostra l'elenco dei comandi disponibili\n"
		"add [file]\n --> invia il file specificato al server remoto\n"
		"compress [alg]\n --> riceve dal server remoto "
		"l'archivio compresso secondo l'algoritmo specificato\n"
		"quit\n --> disconnessione\n"
	);
}

int main(int argc, char *argv[]) {
	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta
	//		argv[1] = indirizzo
	//		argv[2] = porta
	// metto di default porta e indirizzo se argc < 3
	(void)argc;
	(void)argv;
	// TODO: prendi l'indirizzo e la porta da riga di comando
	const char *addr_str = "127.0.0.1";
	int			port_no	 = 10000;
	if (argc == 3) {
		addr_str = argv[1];	 // oppure strdup
		port_no	 = atoi(argv[2]);
	}
	// creazione del socket
	int				   sd;
	struct sockaddr_in sa;
	if (socket_stream(&addr_str, int port_no, int *sd, struct sockaddr_in *sa) < 0) {
		fprintf(stderr, "Impossibile creare il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}

	// loop
	while (1) {
		const unsigned int CMD_MAX = 1024;
		char			   userinput[CMD_MAX];
		printf("Inserire comando:\n");
		if (scanf("%1023s", &userinput) < 0) {
			fprintf(stderr, "Impossibile accettare comando: %s\n", strerror(errno));
			continue;
		}
		// dividi il comando in sotto stringhe divise da ' '
		// "compress b" <- secondo argomento
		//  ^^^^^^^^
		//  primo argomento
		char *cmd;
		char *arg;
		if (read_command(userinput, &cmd, &arg) < 0) {
			fprintf(stderr, "Troppi argomenti\n");
			continue;
		}
		// gestione comandi
		if (strcmp(cmd, "help")) {
			help();
		} else if (strcmp(cmd, "quit")) {
			send_command(sd, "quit", NULL);
			close(sd);	// quit del client
			exit(EXIT_SUCCESS);
		} else if (strcmp(cmd, "compress")) {
			char algoritmo = 'z';
			// bisogna leggere arg, se non e' NULL allora lo cambio
			if (arg != NULL && (strlen(arg) == 1)) {
				algoritmo = arg[0];
			}
			// se esiste un secondo argomento sostituiscilo ad
			// "algoritmo", gli passo direttamente z o j il nome
			// dell'archivio viene scelto automaticamente dal server
			// archivio come enum??
			if (algoritmo == 'z') {
				// comprimi con algoritmo gzip
				send_command(sd, "compress", "cz");
			} else if (algoritmo == 'j') {
				// comprimi con algoritmo bzip2
				send_command(sd, "compress", "cj");
			} else {
				printf("Campo [alg] non valido\n");
				continue;
			}
		} else if (strcmp(cmd, "add")) {
			if (arg == NULL) {
				printf("Campo [file] mancante\n");
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
			for (int i = 0; i < strlen(filename); i++) {
				if (!isascii(filename[i]) && !isalpha(filename[i]) &&
					!isnum(filename[i]) && filename[i] != '.') {
					invalid_name = 1;
					break;
				}
			}

			if (invalid_name) {
				printf("Nome file '%s' non valido\n", filename);
				continue;
			}

			send_command(sd, "add", filename);
			if (send_file(sd, arg) < 0) {
				fprintf(stderr, "Errore nel trasferimento del file %s\n", filename);
				return -1;
			}

			if (receive_response(sd) < 0) {
				fprintf(stderr, "Errore ricevuto dal server %s\n", filename);
				return -1;
			}
		}
	}

	return 0;
}