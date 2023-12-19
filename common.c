#include <sys/stat.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "common.h"

// il socket descriptor da usare
extern int sd;

const int OK = 1;

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

int socket_stream(const char *addr_str, int port_no, struct sockaddr_in *sa) {
	sd = socket(AF_INET, SOCK_STREAM, 0);
	if (sd < 0) {
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

int send_file(const char *path) {
	ssize_t file_dim = file_dimension(path);
	if (file_dim < 0) {
		// l'errore specifico viene stampato da file_dimension()
		// il server si aspetta un file quindi gli devo mandare qualcosa
		// nei casi in cui l'errore e' colpa del file gli mando 0xffffffff

		uint32_t msg_len = UINT32_MAX;
		send(sd, &msg_len, sizeof(uint32_t), 0);
		return -1;
	}

	// se il file è troppo grande non gli posso mandare la dimensione
	if (file_dim >= UINT32_MAX) {
		fprintf(
			stderr,
			"Il file è troppo grande: %.2fGiB\n",
			(float)file_dim / (float)(1024 * 1024 * 1024)
		);
		uint32_t msg_len = UINT32_MAX;
		send(sd, &msg_len, sizeof(uint32_t), 0);
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
	size_t sent_tot = 0;
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
		sent_tot += sent_bytes;
		if (sent_tot >= (size_t)file_dim) {
			break;
		}
	}
	// ascolta la risposta del server
	printf("Verifica risposta dal server...\n");
	if (receive_response() < 0) {
		return -1;
	}

	printf("Inviati %ld/%ld bytes di file\n", sent_bytes, file_dim);

	return 0;
}

int receive_file(const char *path) {
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
			// MANDARE SEND_RESPONSE NO
			fclose(file);
			remove(path);
			return -1;
		}
		fputc(buff[0], file);
		recv_tot += rcvd_bytes;
	}

	fclose(file);
	if (recv_tot != (ssize_t)file_dim) {
		fprintf(
			stderr,
			"Ricezione del file fallita: ricevuti %ld byte in piu' della dimensione "
			"del file\n",
			(recv_tot - file_dim)
		);
		remove(path);
		send_response(!OK);
		return -1;
	} else {
		printf("File %s ricevuto. Ricevuti %ld byte\n", path, recv_tot);
	}
	send_response(OK);
	return 0;
}

int send_response(int ok) {
	if (ok) {
		if (send(sd, "OK", 2, 0) < 0) {
			fprintf(stderr, "Impossibile inviare risposta al client\n");
			return -1;
		}
	} else {
		if (send(sd, "NO", 2, 0) < 0) {
			fprintf(stderr, "Impossibile inviare risposta al client\n");
			return -1;
		}
	}
	return 0;
}

int receive_response() {
	ssize_t rcvd_bytes;
	char	resp[3];
	rcvd_bytes = recv(sd, &resp, 2, 0);
	if (rcvd_bytes < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return -1;
	}
	resp[rcvd_bytes] = '\0';
	if (strcmp(resp, "OK") != 0) {
		fprintf(stderr, "Server segnala il seguente errore: %s\n", resp);
		return -1;
	}
	printf("Ricevuto l'OK dal server\n");

	return 0;
}

int send_command(const char *com, const char *arg) {
	// --- INVIO LUNGHEZZA COMANDO --- //
	// conversione a formato network (da big endian a little endian)
	size_t com_len	  = strlen(com);
	int	   msg_len	  = htonl(com_len);
	size_t sent_bytes = send(sd, &msg_len, sizeof(int), 0);
	if (sent_bytes < 0) {
		fprintf(stderr, "Impossibile inviare dati comando: %s\n", strerror(errno));
		return -1;
	}
	printf("Inviati %ld bytes di lunghezza comando\n", sent_bytes);

	// --- INVIO TESTO COMANDO --- //
	// manda il comando byte per byte
	size_t sent_tot = 0;
	char   buff[1];
	while (1) {
		buff[0]	   = com[sent_tot];
		sent_bytes = send(sd, buff, 1, 0);
		if (sent_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati comando: %s\n", strerror(errno));
			return -1;
		}
		sent_tot += sent_bytes;
		if (sent_tot == (size_t)com_len) {
			break;
		} else if (sent_tot > (size_t)com_len) {  // ROBA IN PIU'!!!
			fprintf(
				stderr,
				"Invio comando fallito: inviati %ld byte in piu' della dimensione del "
				"comando\n",
				(sent_tot - com_len)
			);
			return -1;
		}
	}

	// ascolta la risposta del server
	printf("Verifica risposta dal server...\n");
	if (receive_response() < 0) {
		return -1;
	}
	printf("Inviati %ld bytes di comando\n", sent_tot);

	// --- INVIO LUNGHEZZA ARGOMENTO --- //
	// se il secondo argomento è nullo allora manda solo la sua dimensione, ovvero 0
	if (arg == NULL) {
		int msg_len = 0;
		send(sd, &msg_len, sizeof(int), 0);
	} else {
		size_t arg_len = strlen(arg);
		msg_len		   = htonl(arg_len);
		sent_bytes	   = send(sd, &msg_len, sizeof(int), 0);
		if (sent_bytes < 0) {
			fprintf(stderr, "Impossibile inviare dati argomento: %s\n", strerror(errno));
			return -1;
		}
		printf("Inviati %ld bytes di lunghezza argomento\n", sent_bytes);
		// --- INVIO TESTO ARGOMENTO --- //

		// manda l'argomento byte per byte
		sent_tot = 0;
		while (1) {
			buff[0]	   = arg[sent_tot];
			sent_bytes = send(sd, buff, 1, 0);
			if (sent_bytes < 0) {
				fprintf(
					stderr, "Impossibile inviare dati argomento: %s\n", strerror(errno)
				);
				return -1;
			}
			sent_tot += sent_bytes;
			if (sent_tot == (size_t)arg_len) {
				break;
			} else if (sent_tot > (size_t)arg_len) {  // ROBA IN PIU'!!!
				fprintf(
					stderr,
					"Invio argomento fallito: inviati %ld byte in piu' della dimensione "
					"dell' argomento\n",
					(sent_tot - arg_len)
				);
				return -1;
			}
		}
	}

	// ascolta la risposta del server
	printf("Verifica risposta dal server...\n");
	if (receive_response() < 0) {
		return -1;
	}

	printf("Inviati %ld bytes di argomento\n", sent_tot);

	return 0;
}

int receive_command(char **cmd, char **arg) {
	char *command = NULL, *argument = NULL;

	// --- RICEZIONE LUNGHEZZA COMANDO --- //
	int msg_len = 0, cmd_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return -1;
	}
	cmd_dim = ntohl(msg_len);

	// --- RICEZIONE TESTO COMANDO --- //
	if ((command = malloc((size_t)cmd_dim + 1)) == NULL) {
		fprintf(
			stderr, "Impossibile allocare memoria per il comando: %s\n", strerror(errno)
		);
		return -1;
	}
	command[cmd_dim] = '\0';

	// riceve il comando byte per byte
	ssize_t recv_tot = 0, rcvd_bytes = 0;
	while (recv_tot < (ssize_t)cmd_dim) {
		if ((rcvd_bytes = recv(sd, &command[recv_tot], 1, 0)) < 0) {
			fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
			free(command);
			return -1;
		}
		recv_tot += rcvd_bytes;
	}
	*cmd = command;

	// comando ricevuto, manda riscontro al peer
	send_response(OK);

	// --- RICEZIONE LUNGHEZZA ARGOMENTO --- //
	int arg_dim = 0;
	msg_len		= 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		free(command);
		return -1;
	}
	arg_dim = ntohl(msg_len);

	// --- RICEZIONE TESTO ARGOMENTO --- //
	// argomento di lunghezza zero significa che il comando non prevede argomento
	if (arg_dim == 0) {
		*arg = NULL;
	} else {
		if ((argument = malloc((size_t)arg_dim + 1)) == NULL) {
			fprintf(
				stderr,
				"Impossibile allocare memoria per l'argomento: %s\n",
				strerror(errno)
			);
			free(command);
			return -1;
		}
		argument[arg_dim] = '\0';

		// riceve l'argomento byte per byte
		recv_tot = 0, rcvd_bytes = 0;
		while (recv_tot < (ssize_t)arg_dim) {
			if ((rcvd_bytes = recv(sd, &argument[recv_tot], 1, 0)) < 0) {
				fprintf(
					stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno)
				);
				free(command);
				free(argument);
				return -1;
			}
			recv_tot += rcvd_bytes;
		}
		*arg = argument;
	}
	// argomento ricevuto, manda riscontro al peer
	send_response(OK);

	return 0;
}