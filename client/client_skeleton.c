#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// stampa le funzioni disponibili
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

/* * * * * * * * NOTA: per la questione della CTRL+C * * * * * * * */
// si deve usare la signal dando un handler alla gestione del segnale SIGINT.
// siccome non accetta argomenti, devo trovare un modo per poter pulire i socket aperti
// senza passare il socket descriptor all'handler della SIGINT
// ci sono quattro soluzioni che abbiamo trovato:
// [1] soluzione per i neurotipici: socket descriptor globali
//     quando ricevo ^C posso fare la quit usando il sd senza bisogno di argomenti
// [2] soluzione polling: l'handler setta una flag a 1 che controllo nella main, not great
// [3] soluzione non ISO C compliant molto bellina che ho fatto io quindi la migliore uwu
//     creo una quit che viene definita nella main, che quindi vede sd che sta nella main
/*	   void quit();
		...
	   int main(){
		...
		//dopo aver creato il socket:
			void quit() {
			close(sd);
			printf("ho chiuso il socket\n");
			exit(EXIT_SUCCESS);
			}
		...
		}
*/
// comunque vedere provaquit.c nel client per la prova di funzionamento
// [4] soluzione flex assurdo

// connettiti al server all'indizzo e porta specificata
// addr: stringa che contiene l'indirzzo ip del server a cui connettersi
// port: porta del server
// *sd: ritorna il socket descriptor
// *sa: ritorna la struttura del socket
int connect_to_server(const char *addr, int port, int *sd, struct sockaddr_in *sa) {
	return 0;
}

// manda un comando testuale al server come "quit" e "compress"
// sd: descriptor del socket
// str: stringa che contiene il comando
// esempio: send_command(sd, "exit");
int send_command(int sd, const char *str);

// manda un file al server specificando il suo percorso
// sd: descriptor del socket
// path: percorso del file da inviare
// esempio: send_file(sd, "../test_file.pdf");
// ritorna 0 se successo o -1 per fallimento
int send_file(int sd, const char *path) { return 0; }

// dato un file, ad esempio tramite FILE*, descriptor o percorso, ne determina la
// dimensione, in caso di errore rirorna negativo
ssize_t file_dimension(void) { return 0; }

// aspetta un messaggio di risposta dal server, ritorna 0 se "OK"
// oppure negativo se ci sono stati errori o "NON OK"
int receive_response(int sd) { return 0; }

// apetta un file dal server e lo immagazzina nel file di nome specificato dal
// server, nella posizione specificata da path (deve essere una cartella)
int receive_file(int sd, const char *path) { return 0; }

int main(int argc, char **argv) {
	// leggi gli argomenti dal terminale e determina l'indirizzo del server
	// e la porta

	// loop
	while (1) {
		const unsigned int CMD_MAX = 1024;
		char			   comando[CMD_MAX];
		// prendi il comando da stdin
		// dividi il comando in sotto stringhe divise da ' '
		// "compress b" <- secondo argomento
		//  ^^^^^^^^
		//  primo argomento

		char *cmd1;	 // primo argomento
		if (strcmp(cmd1, "help")) {
			help();
		} else if (strcmp(cmd1, "quit")) {
			send_command(sd, "quit");
			close(sd);
			exit(EXIT_SUCCESS);
		} else if (strcmp(cmd1, "compress")) {
			char algoritmo = 'z';

			// se esiste un secondo argomento sostituiscilo ad
			// "algoritmo", gli passo direttamente z o j il nome
			// dell'archivio viene scelto automaticamente dal server
			// archivio come enum??
			if (algoritmo == 'z') {
				// comprimi con algoritmo gzip
				send_command(sd, "cz");
			} else if (algoritmo == 'j') {
				// comprimi con algoritmo bzip2
				send_command(sd, "cj");
			} else {
				// errore
			}
		} else if (strcmp(cmd1, "add")) {
			char *file;
			// mette in file il secondo argomento
			// verifica che il nome del file sia composto solamente
			// da [a-z]|[A-Z]|[0-9]|(.)
			// opzionale: accetta file con path relativo o assoluto,
			// significa dover separare il nome del file dal suo path
			// /path/to/file
			// ^^^^^^^^ ^^^^
			// path     filename

			if (send_file(sd, file) < 0) {
				// errore nel trasferimento del file
			}

			if (receive_response(sd) < 0) {
				// errore ricevuto dal server
			}
		}
	}

	return 0;
}