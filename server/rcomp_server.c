#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
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