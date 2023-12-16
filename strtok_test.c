const char *command = NULL;
const char *argument = NULL;

int argc;
char *substr;

for (argc = 0; (substr = strtok(str, " ") != NULL); argc++) {
	if (argc == 0) {
		command = substr;
	} else if (argc == 1) {
		argument = substr;
	} else {
		// errore troppi argomenti
	}
}

// str[] = "   pippo caio     foo0";
// dopo:   "000pippo0caio00000foo0"
//             ^p1   ^p2      ^p3 ^p4=NULL
