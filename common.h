#ifndef _RCOMP_COMMON_H
#define _RCOMP_COMMON_H

#include <sys/types.h>

// colori
#define ANSI_COLOR_RED	   "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE	   "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN	   "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define RED(str)	 ANSI_COLOR_RED str ANSI_COLOR_RESET
#define GREEN(str)	 ANSI_COLOR_GREEN str ANSI_COLOR_RESET
#define YELLOW(str)	 ANSI_COLOR_YELLOW str ANSI_COLOR_RESET
#define BLUE(str)	 ANSI_COLOR_BLUE str ANSI_COLOR_RESET
#define MAGENTA(str) ANSI_COLOR_MAGENTA str ANSI_COLOR_RESET
#define CYAN(str)	 ANSI_COLOR_CYAN str ANSI_COLOR_RESET

// controlla il valore di err (errno) e se l'errore indica un problema con il socket
// allora ritorna 1, altrimenti 0
int is_network_error(int err);

// ritorna la dimensione in byte del file specificato da path oppure -1 in caso di
// errore errore si ha nei seguenti casi:
//     1. Il file non è un file regolare
//     1. I motivi per cui stat() può fallire
ssize_t file_dimension(const char *path);

// genera il nome dell'archivio compresso da ricevere/trasmettere con l'estensione
// giusta
int get_filename(char ext, char **file_name);

// crea un socket di tipo AF_INET e SOCK_STREAM, assegna il descriptor alla variabile
// esterna sd e ritorna la struttura sa riempita con l'indirizzo specificato da
// addr_str e port_no
int socket_stream(const char *addr_str, int port_no, int *sd, struct sockaddr_in *sa);

// invia un file al peer specificato dalla variabile esterna sd
int send_file(const char *path);

// riceve un file dal peer e lo salva nel percorso specificato da path
// NOTA: path è il percorso al file, non ad una cartella
int receive_file(const char *path);

// aspetta un messaggio di risposta dal peer, ritorna 0 se "OK"
// oppure negativo se ci sono stati errori o "NON OK"
// di solito usata dopo ogni messaggio, non quello della lunghezza del messaggio
int receive_response();

// manda una risposta al peer, se va tutto bene oppure no
int send_response(int ok);

// manda un comando testuale al server come "quit" e "compress"
// str: stringa che contiene il comando
// esempio: send_command(sd, "exit");
int send_command(const char *com, const char *arg);

// riceve due stringhe, la prima sarà il comando e la seconda l'argomento, le
// immagazzina in *cmd e *arg NOTA: in caso di errore i puntatori NON sono validi e
// non serve liberare memoria
int receive_command(char **cmd, char **arg);

#endif
