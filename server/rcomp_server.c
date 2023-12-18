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

// attraverso la funzione "system()" comprime la cartella dei file del client (path)
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(int sd, const char *path, char alg) {
	char command[1024];

	if (alg == 'z') {
		snprintf(command, 1024, "tar -c -z -f archivio_compresso.tar.gz %s", path);
	} else if (alg == 'j') {
		snprintf(command, 1024, "tar -c -j -f archivio_compresso.tar.bz2 %s", path);
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

// manda una risposta al client, se va tutto bene oppure no
int send_response(int sd, int ok) {
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

// ricevi un file dal client e mettilo nella cartella personale con il nome specificato
int receive_file(int sd, const char *filename) {
	// controlla che la cartella personale esista, se non esiste creala
	char dirname[64];
	snprintf(dirname, 64, "%d", getpid());

	int e = mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (e < 0 && errno != EEXIST) {
		fprintf(
			stderr, "Impossibile creare la cartella di processo: %s\n", strerror(errno)
		);
		return -1;
	}
	// entra nella cartella
	chdir(dirname);

	FILE *file = fopen(filename, "w+");

	// --- RICEZIONE LUNGHEZZA FILE --- //
	uint32_t msg_len = 0, file_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return -1;
	}
	file_dim = ntohl(msg_len);
	// se ho ricevuto un file di lunghezza 0xffffffff significa che il client
	// ha avuto un problema nell'invio
	if (file_dim == UINT32_MAX) {
		fprintf(stderr, "Il client ha avuto problemi nell'invio del file\n");
		fclose(file);
		remove(filename);
		chdir("..");
		return 0;
	}
	printf("Ricevuti %d byte di lunghezza del file\n", file_dim);

	// --- RICEZIONE FILE --- //
	char	buff[1];
	ssize_t rcvd_bytes = 0;
	ssize_t recv_tot   = 0;
	while (recv_tot < (ssize_t)file_dim) {
		if ((rcvd_bytes = recv(sd, &buff, 1, 0)) < 0) {
			fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
			fclose(file);
			remove(filename);
			chdir("..");
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
		remove(filename);
		chdir("..");
		return -1;
	} else {
		printf("Ricevuti %ld byte di file\n", recv_tot);
	}

	chdir("..");
	return 0;
}

// riceve due stringhe dal server, la prima sarà il comando e la seconda l'argomento
// NOTA: in caso di errore i puntatori NON sono validi e non serve liberare memoria
int receive_command(int sd, char **cmd, char **arg) {
	char *command = NULL, *argument = NULL;

	// --- RICEZIONE LUNGHEZZA COMANDO --- //
	int msg_len = 0, cmd_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		return -1;
	}
	cmd_dim = ntohl(msg_len);

	// --- RICEZIONE COMANDO --- //
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

	// --- RICEZIONE LUNGHEZZA ARGOMENTO --- //
	int arg_dim = 0;
	msg_len		= 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		free(command);
		return -1;
	}
	arg_dim = ntohl(msg_len);

	// argomento di lunghezza zero significa che il comando non prevede argomento
	if (arg_dim == 0) {
		*arg = NULL;
		return 0;
	}

	// --- RICEZIONE ARGOMENTO --- //
	if ((argument = malloc((size_t)arg_dim + 1)) == NULL) {
		fprintf(
			stderr, "Impossibile allocare memoria per l'argomento: %s\n", strerror(errno)
		);
		free(command);
		return -1;
	}
	argument[arg_dim] = '\0';

	// riceve l'argomento byte per byte
	recv_tot = 0, rcvd_bytes = 0;
	while (recv_tot < (ssize_t)arg_dim) {
		if ((rcvd_bytes = recv(sd, &argument[recv_tot], 1, 0)) < 0) {
			fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
			free(command);
			free(argument);
			return -1;
		}
		recv_tot += rcvd_bytes;
	}
	*arg = argument;

	return 0;
}
