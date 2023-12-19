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

const int ok = 1;
int sd = -1;

void quit() {
		;
		if (close(sd) < 0) {
            fprintf(stderr, "Impossibile chiudere il socket: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
		printf("Chiusura del socket avvenuta con successo\n");
		exit(EXIT_SUCCESS);
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

int send_file(const char *path) {
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
	return 0;
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

// attraverso la funzione "system()" comprime la cartella dei file del client
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(char alg) {
	char command[1024];
	char dirname[64];
	snprintf(dirname, 64, "%d", getpid());

	if (alg == 'z') {
		snprintf(command, 1024, "tar -c -z -f archivio_compresso.tar.gz %s", dirname);
	} else if (alg == 'j') {
		snprintf(command, 1024, "tar -c -j -f archivio_compresso.tar.bz2 %s", dirname);
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

// ricevi un file dal client e mettilo nella cartella personale con il nome specificato
int receive_file(const char *filename) {
	// controlla che la cartella personale esista, se non esiste creala
	char dirname[64];
	snprintf(dirname, 64, "%d", getpid());

	int e = mkdir(dirname, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
	if (e < 0 && errno != EEXIST) {
		fprintf(
			stderr, "Impossibile creare la cartella di processo: %s\n", strerror(errno)
		);
		send_response(!ok);
		return -1;
	}
	// entra nella cartella
	chdir(dirname);

	FILE *file = fopen(filename, "w+");

	// --- RICEZIONE LUNGHEZZA FILE --- //
	uint32_t msg_len = 0, file_dim = 0;
	if (recv(sd, &msg_len, sizeof(int), 0) < 0) {
		fprintf(stderr, "Impossibile ricevere dati su socket: %s\n", strerror(errno));
		chdir("..");
		send_response(!ok);
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
			send_response(!ok);
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
		send_response(!ok);
		return -1;
	} else {
		printf("Ricevuti %ld byte di file\n", recv_tot);
	}

	chdir("..");
	send_response(ok);
	return 0;
}

// riceve due stringhe dal server, la prima sarà il comando e la seconda l'argomento
// NOTA: in caso di errore i puntatori NON sono validi e non serve liberare memoria
int receive_command(char **cmd, char **arg) {
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

// gestisce il socket di un client
int client_process(void) {
	while (1) {
		char *cmd, *arg;
		if (receive_command(&cmd, &arg) < 0) {
			fprintf(stderr, "Impossibile ricevere il comando\n");
			continue;
		}

		if (strcmp(cmd, "quit")) {
			send_response(ok);
			// rimuovi la cartella
			// visto che pid e' al massimo un numero di 5 caratteri, creo un
			// buffer di un ordine di grandezza maggiore: 64
			// 64 eè piu' bello di 32 e 16 :)
			char command[sizeof("rm -rf ") + 64];
			snprintf(command, sizeof("rm -rf ") + 64, "rm -rf %d", getpid());
			system(command);
			break;
		} else if (strcmp(cmd, "add")) {
			// controllo di avere il nome del file
			char *filename = arg;
			if (filename == NULL) {
				fprintf(stderr, "Ricevuto il comando add senza file\n");
				continue;
			}

			if (receive_file(filename) < 0) {
				fprintf(stderr, "Impossibile ricevere il file %s\n", filename);
				continue;
			}

			continue;
		} else if (strcmp(cmd, "compress")) {
			// controllo di avere il nome del file
			char *alg = arg;
			if (alg == NULL) {
				fprintf(stderr, "Ricevuto il comando compress senza file\n");
				continue;
			}

			if (compress_folder(alg[0]) < 0) {
				continue;
			}

			continue;
		}
	}

	return 0;
}

int main(int argc, char *argv[]) {
	(void)argc;
	(void)argv;
	char *addr_str = "127.0.0.1";
	int	  port_no  = 10000;

	// --- CREAZIONE SOCKET --- //
	struct sockaddr_in *sa;
	if (socket_stream(*addr_str, port_no, *sa) < 0) {
		printf(stderr, "Impossibile creare il socket: %s\n", strerror(errno));
		exit(EXIT_FAILURE);
	}
	// --- BINDING --- //

	// associazione indirizzo a socket
	if (bind(sd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
		fprintf(
			stderr, "Impossibile associare l'indirizzo a un socket: %s\n", strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	printf("Socket %d associato a %s:%d\n", sd, addr_str, port_no);

	// --- LISTENING --- //
	if (listen(sd, 10) < 0) {
		fprintf(
			stderr, "Impossibile mettersi in attesa su socket: %s\n", strerror(errno)
		);
		exit(EXIT_FAILURE);
	}

	// --- ATTESA DI CONNESSIONE --- //
	printf("--- In attesa di connessione ---\n");

	int				   conn_sd;
	struct sockaddr_in client_addr;
	char			   client_addr_str[INET_ADDRSTRLEN];

	while (1) {
		socklen_t client_addr_len = sizeof(client_addr);
		conn_sd = accept(sd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (conn_sd < 0) {
			fprintf(
				stderr,
				"Impossibile accettare connessione su socket: %s\n",
				strerror(errno)
			);
			continue;
		}

		// conversione dell'indirizzo in formato numerico
		const char *res = inet_ntop(
			AF_INET, &client_addr.sin_addr.s_addr, client_addr_str, INET_ADDRSTRLEN
		);
		if (res == NULL) {
			fprintf(stderr, "Impossibile convertire l'indirizzo: %s\n", strerror(errno));
		} else {
			printf("Connesso col client %s:%d\n", addr_str, ntohs(client_addr.sin_port));
		}

		pid_t pid;
		if ((pid = fork()) < 0) {
			fprintf(
				stderr, "Impossibile creare un processo figlio: %s\n", strerror(errno)
			);
			close(conn_sd);
			close(sd);
			exit(EXIT_FAILURE);
		} else if (pid == 0) {
			// processo figlio
			process_client(conn_sd);
			exit(EXIT_SUCCESS);
		} else {
			// processo genitore
			if (wait(NULL) < 0) {
				fprintf(
					stderr, "Impossibile creare un processo figlio: %s\n", strerror(errno)
				);
			}
			printf("Il processo %d ha terminato", pid);
		}
	}

	// --- CHIUSURA SOCKET --- //
	close(sd);

	return EXIT_SUCCESS;
}
