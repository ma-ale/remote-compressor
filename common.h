#ifndef _RCOMP_COMMON_H
#define _RCOMP_COMMON_H

#include <sys/types.h>

// ritorna la dimensione in byte del file specificato da path oppure -1 in caso di errore
// errore si ha nei seguenti casi:
//     1. Il file non è un file regolare
//     1. I motivi per cui stat() può fallire
ssize_t file_dimension(const char *path);

// crea un socket di tipo AF_INET e SOCK_STREAM, assegna il descriptor alla variabile
// esterna sd e ritorna la struttura sa riempita con l'indirizzo specificato da addr_str
// e port_no
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

// riceve due stringhe, la prima sarà il comando e la seconda l'argomento, le immagazzina
// in *cmd e *arg
// NOTA: in caso di errore i puntatori NON sono validi e non serve liberare memoria
int receive_command(char **cmd, char **arg);

#endif