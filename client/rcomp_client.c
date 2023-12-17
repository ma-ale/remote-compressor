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
			fprintf(stderr, "Argomento di troppo: [%d] = '%s'\n", argc, tmp);
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

	size_t snd_bytes = send(sd, &msg_len, sizeof(uint32_t), 0);
	if (snd_bytes < 0) {
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
		buff[0]	  = c;
		snd_bytes = send(sd, buff, 1, 0);
		if (snd_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati: %s\n", strerror(errno));
			return -1;
		}
		bytes_sent += snd_bytes;
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

// manda un comando testuale al server come "quit" e "compress"
// sd: descriptor del socket
// str: stringa che contiene il comando
// esempio: send_command(sd, "exit");
int send_command(int sd, const char *com, const char *arg) {
	// --- INVIO LUNGHEZZA --- //
	// conversione a formato network (da big endian a little endian)
	size_t com_len	 = strlen(com);
	int	   msg_len	 = htonl(com_len);
	size_t snd_bytes = send(sd, &msg_len, sizeof(int), 0);
	if (snd_bytes < 0) {
		fprintf(stderr, "Impossibile inviare dati comando: %s\n", strerror(errno));
		return (-1);
	}
	printf("Inviati %ld bytes di lunghezza comando\n", snd_bytes);
	// --- INVIO COMANDO --- //
	// manda il comando byte per byte
	size_t bytes_sent = 0;
	char   buff[1];
	while (1) {
		buff[0]	  = com[bytes_sent];
		snd_bytes = send(sd, buff, 1, 0);
		if (snd_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati comando: %s\n", strerror(errno));
			return (-1);
		}
		bytes_sent += snd_bytes;
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
	snd_bytes	   = send(sd, &msg_len, sizeof(int), 0);
	if (snd_bytes < 0) {
		fprintf(stderr, "Impossibile inviare dati argomento: %s\n", strerror(errno));
		return (-1);
	}
	printf("Inviati %ld bytes di lunghezza argomento\n", snd_bytes);
	// --- INVIO ARGOMENTO --- //

	// manda l'argomento byte per byte
	bytes_sent = 0;
	while (1) {
		buff[0]	  = arg[bytes_sent];
		snd_bytes = send(sd, buff, 1, 0);
		if (snd_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati argomento: %s\n", strerror(errno));
			return (-1);
		}
		bytes_sent += snd_bytes;
		if (bytes_sent == (size_t)arg_len) {
			break;
		} else if (bytes_sent > (size_t)arg_len) {
			printf(
				"Invio argomento fallito: inviati %ld byte in piu' della dimensione "
				"dell' argomento\n",
				(bytes_sent - arg_len)
			);
			return (-1);
		}
	}
	printf("Inviati %ld bytes di argomento\n", bytes_sent);

	return 0;
}