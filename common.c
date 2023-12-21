#define _XOPEN_SOURCE	500
#define _POSIX_C_SOURCE 200809L

#include <sys/stat.h>
#include <arpa/inet.h>

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "common.h"

#define CHUNK_SIZE 4096

const int OK = 1;

int is_network_error(int err) {
	switch (err) {
	case EPIPE:			// se il peer ha chiuso la connessione
	case ECONNABORTED:	// se la connessione è stata interrotta
	case ECONNREFUSED:
	case EBADF:	 // se il socket è già stato chiuso
		fprintf(stderr, RED("\tERRORE: Errore di connessione (%s)\n"), strerror(errno));
		return 1;
	default:
		return 0;
	}
}

ssize_t file_dimension(const char *path) {
	// recupero dei metadati del file
	struct stat file_stat;
	if (stat(path, &file_stat) < 0) {
		fprintf(
			stderr,
			MAGENTA(
				"\tERRORE: Errore nella lettura delle informazioni del file '%s': %s\n"
			),
			path,
			strerror(errno)
		);
		return -1;
	}

	// se il file e' un file regolare, visualizza la sua dimensione
	ssize_t file_size;
	if (S_ISREG(file_stat.st_mode) > 0) {
		file_size = file_stat.st_size;
	} else {
		printf(MAGENTA("\tERRORE: Il file non e' un file regolare\n"));
		return -1;
	}
	return file_size;
}

int get_filename(char ext, char **file_name) {
	char base_name[64] = "archivio_compresso.tar";
	if (ext == 'z') {
		strcat(base_name, ".gz");
	} else if (ext == 'j') {
		strcat(base_name, ".bz2");
	} else {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Estensione file non "
					"riconosciuta: %c\n"),
			ext
		);
		return -1;
	}
	*file_name = strdup(base_name);	 // linuk non fallisce mai di memoria ;)
	return 0;
}

int socket_stream(const char *addr_str, int port_no, int *sd, struct sockaddr_in *sa) {
	*sd = socket(AF_INET, SOCK_STREAM, 0);
	if (*sd < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile creare il socket: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	// conversione dell'indirizzo in formato numerico
	in_addr_t address;
	if (inet_pton(AF_INET, addr_str, (void *)&address) <= 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile convertire l'indirizzo '%s': %s\n"),
			addr_str,
			strerror(errno)
		);
		return -1;
	}
	// preparazione della struttura contenente indirizzo IP e porta
	sa->sin_family		= AF_INET;
	sa->sin_port		= htons(port_no);
	sa->sin_addr.s_addr = address;

	return 0;
}

int send_file(int sd, const char *path) {
	printf("\tInvio del file %s al server in corso...\n", path);
	ssize_t file_dim = file_dimension(path);
	if (file_dim < 0) {
		// l'errore specifico viene stampato da file_dimension()
		// il server si aspetta un file quindi gli devo mandare qualcosa
		// nei casi in cui l'errore e' colpa del file gli mando 0xffffffff

		uint32_t msg_len = UINT32_MAX;
		send(sd, &msg_len, sizeof(uint32_t), 0);
		fprintf(stderr, MAGENTA("\tERRORE: Inviato 0xffffffff come dimensione file\n"));

		return -1;
	}

	// se il file è troppo grande non gli posso mandare la dimensione
	if (file_dim >= UINT32_MAX) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Il file è troppo grande: %.2fGiB\n"),
			(float)file_dim / (float)(1024 * 1024 * 1024)
		);
		uint32_t msg_len = UINT32_MAX;
		send(sd, &msg_len, sizeof(uint32_t), 0);
		fprintf(stderr, MAGENTA("\tERRORE: Inviato 0xffffffff come dimensione file\n"));
		return -1;
	}

	uint32_t msg_len = htonl(file_dim);

	ssize_t sent_bytes = send(sd, &msg_len, sizeof(uint32_t), 0);
	if (sent_bytes < 0) {
		fprintf(
			stderr, MAGENTA("\tERRORE: Impossibile inviare dati: %s\n"), strerror(errno)
		);
		return -1;
	}
	printf(YELLOW("\tInviati %ld bytes di lunghezza file\n"), sent_bytes);

	// lo devo aprire come file binary altrimenti la conversione implicita a file di testo
	// che avviene nei sistemi non-UNIX tronca il file a zero, in sostanza se facessi solo
	// fopen(archivio, "r"); in windows archivio viene troncato >:(
	FILE *file = fopen(path, "rb");

	// manda il file
	ssize_t sent_tot = 0;
	char	buff[CHUNK_SIZE];
	while (1) {
		ssize_t bytes_read = read(fileno(file), buff, CHUNK_SIZE);
		if (bytes_read <= 0) {
			break;
		}

		sent_bytes = send(sd, buff, bytes_read, 0);
		if (sent_bytes < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile inviare dati: %s\n"),
				strerror(errno)
			);
			return -1;
		}

		sent_tot += sent_bytes;
		printf(YELLOW("\tInviati: %ld/%ld\n"), sent_tot, file_dim);
		if (sent_tot >= file_dim) {
			break;
		}
	}

	// ascolta la risposta del server
	printf(YELLOW("\tVerifica risposta dal server...\n"));
	if (receive_response(sd) < 0) {
		return -1;
	}

	if (sent_tot == file_dim) {
		printf("\tFile %s inviato\n", path);
	}

	return 0;
}

int receive_file(int sd, const char *path) {
	// --- RICEZIONE LUNGHEZZA FILE --- //
	uint32_t msg_len = 0, file_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	file_dim = ntohl(msg_len);

	// il client ci dice che ha riscontrato un errore
	if (file_dim == UINT32_MAX) {
		fprintf(stderr, MAGENTA("\tERRORE: Il client ha cancellato l'invio del file\n"));
		return -1;
	}
	printf(YELLOW("\tRicevuti %d byte di lunghezza del file\n"), file_dim);

	// apri o crea il file specificato da path in scrittura, troncato a zero
	FILE *file = fopen(path, "w+b");
	if (file == NULL) {
		fprintf(
			stderr, MAGENTA("\tERRORE: Impossibile aprire il file: %s\n"), strerror(errno)
		);
		return -1;
	}

	// --- RICEZIONE FILE --- //
	char	buff[CHUNK_SIZE];
	ssize_t rcvd_bytes = 0;
	ssize_t recv_tot   = 0;
	while (recv_tot < (ssize_t)file_dim) {
		rcvd_bytes = recv(sd, buff, CHUNK_SIZE, 0);
		if (rcvd_bytes < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
				strerror(errno)
			);
			// send_response(!OK);
			fclose(file);
			remove(path);
			return -1;
		} else if (rcvd_bytes == 0) {
			break;
		}
		size_t bytes_written = write(fileno(file), buff, rcvd_bytes);

		if (bytes_written < (size_t)rcvd_bytes) {
			fprintf(stderr, MAGENTA("\tERRORE: Errore nella scrittura del file\n"));
			fclose(file);
			remove(path);
			return -1;
		}
		recv_tot += rcvd_bytes;
		printf(YELLOW("\tRicevuti %ld/%d\n"), recv_tot, file_dim);
	}

	fclose(file);
	if (recv_tot != (ssize_t)file_dim) {
		fprintf(
			stderr,
			MAGENTA("\tRicezione del file fallita: ricevuti %ld byte in piu' "
					" della dimensione del file\n"),
			(recv_tot - file_dim)
		);
		remove(path);
		if (send_response(sd, !OK) < 0) {
			return -1;
		}
		return -1;
	}

	printf("\tFile %s " YELLOW("di %ld byte") " ricevuto\n", path, recv_tot);
	if (send_response(sd, OK) < 0) {
		return -1;
	}
	return 0;
}

int send_response(int sd, int ok) {
	if (ok) {
		if (send(sd, "OK", 2, 0) < 0) {
			fprintf(
				stderr, MAGENTA("\tERRORE: Impossibile inviare risposta al client\n")
			);
			return -1;
		}
	} else {
		if (send(sd, "UH", 2, 0) < 0) {
			fprintf(
				stderr, MAGENTA("\tERRORE: Impossibile inviare risposta al client\n")
			);
			return -1;
		}
	}
	return 0;
}

int receive_response(int sd) {
	ssize_t rcvd_bytes;
	char	resp[3];
	rcvd_bytes = recv(sd, resp, 2, 0);
	if (rcvd_bytes < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	resp[rcvd_bytes] = '\0';
	if (strcmp(resp, "OK") != 0) {
		fprintf(
			stderr, MAGENTA("\tERRORE: Server segnala il seguente errore: %s\n"), resp
		);
		return -1;
	}
	printf(YELLOW("\tRicevuto l'OK dal server\n"));

	return 0;
}

int send_command(int sd, const char *com, const char *arg) {
	// --- INVIO LUNGHEZZA COMANDO --- //
	// conversione a formato network (da big endian a little endian)
	ssize_t com_len	   = strlen(com);
	int		msg_len	   = htonl(com_len);
	ssize_t sent_bytes = send(sd, &msg_len, sizeof(int), 0);
	if (sent_bytes < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile inviare dati comando: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	printf(YELLOW("\tInviati %ld bytes di lunghezza comando\n"), sent_bytes);

	// --- INVIO TESTO COMANDO --- //
	// manda il comando byte per byte

	sent_bytes = send(sd, com, com_len, 0);
	if (sent_bytes < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile inviare dati comando: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	// ascolta la risposta del server
	printf(YELLOW("\tVerifica risposta dal server...\n"));
	if (receive_response(sd) < 0) {
		return -1;
	}
	printf(YELLOW("\tInviati %ld bytes di comando\n"), sent_bytes);

	// --- INVIO LUNGHEZZA ARGOMENTO --- //
	// se il secondo argomento è nullo allora manda solo la sua dimensione, ovvero 0
	if (arg == NULL) {
		int msg_len = 0;
		send(sd, &msg_len, sizeof(int), 0);
	} else {
		ssize_t arg_len = strlen(arg);
		msg_len			= htonl(arg_len);
		sent_bytes		= send(sd, &msg_len, sizeof(int), 0);
		if (sent_bytes < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile inviare dati argomento: %s\n"),
				strerror(errno)
			);
			return -1;
		}
		printf(YELLOW("\tInviati %ld bytes di lunghezza argomento\n"), sent_bytes);

		// --- INVIO TESTO ARGOMENTO --- //
		// manda l'argomento
		sent_bytes = send(sd, arg, arg_len, 0);
		if (sent_bytes < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile inviare dati argomento: %s\n"),
				strerror(errno)
			);
			return -1;
		}
	}

	// ascolta la risposta del server
	printf(YELLOW("\tVerifica risposta dal server...\n"));
	if (receive_response(sd) < 0) {
		return -1;
	}

	printf(YELLOW("\tInviati %ld bytes di argomento\n"), sent_bytes);

	return 0;
}

int receive_command(int sd, char **cmd, char **arg) {
	char *command = NULL, *argument = NULL;

	// --- RICEZIONE LUNGHEZZA COMANDO --- //
	int msg_len = 0, cmd_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	cmd_dim = ntohl(msg_len);

	// --- RICEZIONE TESTO COMANDO --- //
	if ((command = malloc(cmd_dim + 1)) == NULL) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile allocare memoria per il comando: %s\n"),
			strerror(errno)
		);
		return -1;
	}
	command[cmd_dim] = '\0';

	// ricevi il comando
	if (recv(sd, command, cmd_dim, 0) < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
			strerror(errno)
		);
		free(command);
		return -1;
	}
	*cmd = command;

	// comando ricevuto, manda riscontro al peer
	if (send_response(sd, OK) < 0) {
		return -1;
	}

	// --- RICEZIONE LUNGHEZZA ARGOMENTO --- //
	int arg_dim = 0;
	msg_len		= 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(
			stderr,
			MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
			strerror(errno)
		);
		free(command);
		return -1;
	}
	arg_dim = ntohl(msg_len);

	// --- RICEZIONE TESTO ARGOMENTO --- //
	// argomento di lunghezza zero significa che il comando non prevede argomento
	if (arg_dim == 0) {
		*arg = NULL;
	} else {
		if ((argument = malloc(arg_dim + 1)) == NULL) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile allocare memoria per l'argomento: %s\n"),
				strerror(errno)
			);
			free(command);
			return -1;
		}
		argument[arg_dim] = '\0';

		// ricevi l'argomento
		if (recv(sd, argument, arg_dim, 0) < 0) {
			fprintf(
				stderr,
				MAGENTA("\tERRORE: Impossibile ricevere dati su socket: %s\n"),
				strerror(errno)
			);
			free(command);
			free(argument);
			return -1;
		}
	}
	*arg = argument;

	// argomento ricevuto, manda riscontro al peer
	if (send_response(sd, OK) < 0) {
		return -1;
	}
	printf(("\tRicezione comando %s \n"), command);

	return 0;
}
