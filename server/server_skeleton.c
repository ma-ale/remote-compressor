#include <sys/socket.h>
#include <arpa/inet.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// il processo per client fa le seguenti cose:
// 1 - aspetta di ricevere comandi
// 2 - l'add crea una cartella nominata col pid del client
//     (in genere la add cotrolla se c'è già la cartella o no, se c'è mette
//      direttamente lì il file)
// 3 - l'add riceve un file crea tale file nella sua cartella e lo riempie con il
//     file ricevuto byte per byte
//     3.1 - riceve il file dalla rete byte per byte scrivendolo su un buffer
//           temporaneo in memoria, quando questo buffer si riempie o tutto il
//           file è stato ricevuto fa una scrittura sul disco del buffer
// 4 - quando riceve il comando compress spawna un processo figlio che comprime
//     la cartella con i file ricevuti
// 5 - una volta finita la compressione manda l'archivio al client
// 6 - una volta che il client si disconnette pulisce la cartella, la elimina e il
//     processo termina
// Roba opzionale
// - se riceve il comando "clean" pulisce la cartella
// - se riceve il comando "remove" elimina solo i file specificati
// - se riceve il comando "list" fornisce la lista di file presenti nella cartella
// - il comando "compress" può avere una lista opzionale di file da comprimere
int client_process(int sd, struct sockaddr_in sa) { return 0; }

// attraverso la funzione "system()" comprime la cartella dei file del client (path)
// con l'algoritmo specificato in un archivio chiamato "archivio_compresso.tar.gz"
// oppure "archivio_compresso.tar.bz2"
int compress_folder(int sd, const char *path) { return 0; }

// manda una risposta al client, se va tutto bene oppure no
int send_response(int sd, );

// riceve il file dal client
int receive_file(int sd, const char* file_name);
// il server spawna un processo figlioche gestisce un singolo client
int main(int argc, char **argv) { return 0; }
