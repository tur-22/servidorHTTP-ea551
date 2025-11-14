#include <stdio.h>
#include <stdlib.h>	
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/stat.h>
#include <sys/time.h>

#define GET 0
#define HEAD 1
#define OPTIONS 2
#define TRACE 3

#define MAXSIZE 16384

/*ARRUMAR CONST*/
static char * e400 = "<!DOCTYPE html><html><head><title>400 Bad Request</title></head><body><h1>400 Bad Request</h1><p>Aqui est&aacute; o conte&uacute;do de 400.html.</p></body></html>";
static char * e403 = "<!DOCTYPE html><html><head><title>403 Forbidden</title></head><body><h1>403 Forbidden</h1><p>Aqui est&aacute; o conte&uacute;do de 403.html.</p></body></html>";
static char * e404 = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>Aqui est&aacute; o conte&uacute;do de 404.html.</p></body></html>";
static char * e503 = "<!DOCTYPE html><html><head><title>503 Service Unavailable</title></head><body><h1>503 Service Unavailable</h1><p>O servidor est&aacute; sobrecarregado. Tente novamente.</p></body></html>";

static char * concatena(const char *str1, const char *str2);
static char * simplifica_path(char * path);
static void get_date(char *buf, int bufsize);
static void registra_head(char *buf, int registrofd);
static int build_head_err_trace(char *buf, int status, size_t content_length, const char *connection_type);
static int build_head_ok(char *buf, struct stat statinfo, const char *connection_type);
static void entrega_recurso_head(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd);
static void entrega_recurso_get(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd);
static void entrega_recurso(char * path, struct stat statinfo, const char *connection_type, int req_code, int saidafd, int registrofd);
static int trata_gethead(const char *path, const char *resource, const char *connection_type, int req_code, int saidafd, int registrofd);
static void trata_options(const char *connection_type, int saidafd, int registrofd);
static void trata_trace(const char *request, const char *connection_type, int saidafd, int registrofd);

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd);
int process_request(const char *webspace, const char *request,  const char *req_type, const char *resource, const char *connection_type, int saidafd, int registrofd);

static char * concatena(const char *str1, const char *str2) { // const: prometo não alterar conteúdo de strings
	/* Concatena duas strings em um novo ponteiro para char */

	char *str;
	size_t total_len = strlen(str1) + strlen(str2) + 1; // comprimento total das strings + \0

	if (!(str = malloc(total_len))) { // aloca string composta e verifica erro
		perror("(concatena) Erro de alocação de memória");
		exit(errno);
	}

	strcpy(str, str1); // copia e concatena strings
	strcat(str, str2);

	return str;
}

static char * simplifica_path(char * path) {
	/* Interpreta ".." e "." em path e devolve path simplificado */
	/* Supondo que path não é vazio e começa com '/' */
	char *pilha[MAXSIZE];
	int topo = -1;

	char s[MAXSIZE];
	int i = 0, j = 0, len = strlen(path);
	while (i < len) {
		while (path[i] != '/' && i < len) { // lê até próximo '/' e guarda em s
			s[j] = path[i];
			i++;
			j++;
		}
		s[j] = '\0';
		if (!strcmp(s, "..") && pilha[topo] >= 0) { // se s == "..", remove topo da pilha, se existir
			free(pilha[topo]);
			topo--;
		}
		else if (strcmp(s, ".") && strcmp(s, "..") && strlen(s) > 0) // coloca s no topo da pilha se não for "." ou ".." ou vazio
			pilha[++topo] = strdup(s);
		j = 0;
		i++;
	}

	char *ret;

	s[0] = '\0';
	for (int i = 0; i < topo+1; i++) { // desempilha campos do path e os adiciona em string
		strcat(s, "/");
		strcat(s, pilha[i]);
		free(pilha[i]);
	}
	
	ret = strdup(s);
	free(path);
	return ret;
}

static void get_date(char *buf, int bufsize) {
	/* Escreve em buf string formatada de data */

	struct timeval tv; // modificado por gettimeofday
	
	if (gettimeofday(&tv, NULL) == -1) {
        perror("(get_date) Erro em gettimeofday");
        exit(errno);
    }

	strftime(buf, bufsize, "%c BRT", localtime(&tv.tv_sec)); // localtime retorna struct com data atual formatada.

	/*Pelo menos na minha máquina, campo tm_zone de local_time_info armazena fuso-horário no formato -03 e não BRT.
	Por causa disso, inseri "BRT" manualmente no header. Espero que o programa não seja testado em outro país...*/
}

static void registra_head(char *buf, int registrofd) {
	/* Escreve cabeçalho de resposta em registrofd */

	char separador1[] = "------------------------------------------------\n"; // separa requisição de resposta
	char separador2[] = "__________________Nova Entrada__________________\n"; // separa entradas em registro.txt

	if (write(registrofd, separador1, strlen(separador1)) == -1) {
		perror("(registra_head) Erro em write");
		exit(errno);
	}

	if (write(registrofd, buf, strlen(buf)) == -1) {
		perror("(registra_head) Erro em write");
		exit(errno);
	}

	if (write(registrofd, separador2, strlen(separador2)) == -1) {
		perror("(registra_head) Erro em write");
		exit(errno);
	}
}

// DRY: trocar por apenas uma função.

static int build_head_err_trace(char *buf, int status, size_t content_length, const char *connection_type) {
	/* Cabeçalho sem Last-Modified.
	Utilizado para respostas de erro ou trace.*/
	
	char datebuf[64]; // guarda data atual formatada (sem \r\n)
	char statusmsg[32]; // guarda mensagem referente ao status code, a ser exibida na primeira linha da resposta
	char content_type[32];
	int off; // número de caracteres escritos em buf para armazenar header

	get_date(datebuf, sizeof(datebuf));
	
	switch (status) {
		case 200: // requisição trace
			sprintf(statusmsg, "200 OK");
			sprintf(content_type, "message/http");
			break;
		case 400: // por enquanto apenas se req não reconhecida
			sprintf(statusmsg, "400 Bad Request");
			sprintf(content_type, "text/html");
			break;
		case 403:
			sprintf(statusmsg, "403 Forbidden");
			sprintf(content_type, "text/html");
			break;
		case 404:
			sprintf(statusmsg, "404 Not Found");
			sprintf(content_type, "text/html");
			break;
		case 503:
			sprintf(statusmsg, "503 Service Unavailable");
			sprintf(content_type, "text/html");
			break;
		default:
			sprintf(statusmsg, "000"); // caso não tratado
	}

	off = sprintf(
		buf,
		"HTTP/1.1 %s\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nContent-Length: %ld\r\nContent-Type: %s\r\n\r\n",
		statusmsg,
		datebuf,
		connection_type,
		content_length,
		content_type
	);

	return off;

	/*operações de formatação de data feitas com ajuda do chatGPT*/
}

static int build_head_ok(char *buf, struct stat statinfo, const char *connection_type) {
	/*Escreve cabeçalho de resposta a GET ou HEAD bem sucedidos em buf*/

	char datebuf[64]; // guarda data atual formatada (sem \r\n)
	char modtbuf[64]; // guarda data de última modificação formatada (sem \r\n)
	int off; // número de caracteres escritos em buf para armazenar header

	get_date(datebuf, sizeof(datebuf));

	strftime(modtbuf, sizeof(modtbuf), "%c BRT", localtime(&statinfo.st_mtim.tv_sec)); // localtime retorna struct com tempo de última modificação formatado

	off = sprintf(
		buf,
		"HTTP/1.1 200 OK\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nLast-Modified: %s\r\nContent-Length: %ld\r\nContent-Type: text/html\r\n\r\n",
		datebuf,
		connection_type,
		modtbuf,
		statinfo.st_size
	);

	return off;

	/*operações de formatação de data feitas com ajuda do chatgpt*/
}

// DRY?

static void entrega_recurso_head(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd) {
	/* constrói buffer com cabeçalho e o escreve na saída */

	char buf[MAXSIZE];
	int i = build_head_ok(buf, statinfo, connection_type);

	if (write(saidafd, buf, i) == -1) {
		free(path);
		perror("(entrega_recurso_head) Erro em write");
		exit(errno);
	}

	registra_head(buf, registrofd);
}

static void entrega_recurso_get(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd) {
	/* constrói buffer com cabeçalho e recurso e o escreve na saída */
	
	int fd, i;
	int off; // offset a partir do qual read deve escrever em buf, devido ao cabeçalho
	char buf[MAXSIZE];

	off = build_head_ok(buf, statinfo, connection_type);

	registra_head(buf, registrofd); // registra saída em registro.txt antes de ler recurso

	if ((fd = open(path, O_RDONLY)) == -1) {
		free(path);
		perror("(entrega_recurso_get) Erro em open (entrega_recurso_get)");
		exit(errno);
	}
	if ((i = read(fd, buf + off, sizeof(buf) - off)) == -1) {
		free(path);
		perror("(entrega_recurso_get) Erro em read");
		exit(errno);
	}
	if (write(saidafd, buf, i + off) == -1) {
		free(path);
		perror("(entrega_recurso_get) Erro em write");
		exit(errno);
	}
}

static void entrega_recurso(char * path, struct stat statinfo, const char *connection_type, int req_code, int saidafd, int registrofd) {
	/* Chama função correspondente para imprimir resposta GET ou HEAD */

	switch (req_code) {
		case GET:
			entrega_recurso_get(path, statinfo, connection_type, saidafd, registrofd);
			break;
		case HEAD:
			entrega_recurso_head(path, statinfo, connection_type, saidafd, registrofd);
			break;
		default:
			fprintf(stderr, "Erro inesperado em trata_gethead().\n"); // req_code não corresponde a GET ou HEAD
			exit(2);
	}

}

static int trata_gethead(const char *path, const char *resource, const char *connection_type, int req_code, int saidafd, int registrofd) {
	/*responde requisição get ou head, ou retorna status code de erro*/
	// req_code == 0: GET; req_code == 1: HEAD

	char *full_path = concatena(path, resource);
	full_path = simplifica_path(full_path); // simplifica path antes de verificar se inicia com path do webspace
	struct stat statinfo;	

	if (strncmp(path, full_path, strlen(path))) { // verifica se full_path inicia com path do webspace
		/* TODO: '/' deveria ser incluído no fim de path antes da comparação. Ex: /../meu-webspace2 */
		free(full_path);
		return 403; // caso não inicie: forbidden
	}

	if (stat(full_path, &statinfo) == -1) { // chama stat e verifica se houve erro
		switch (errno) { // stat modifica errno com base em erro
			case EFAULT: case ENOTDIR: case ENOENT: // se errno indica erro no path, return 404 
				free(full_path);
				return 404;
			case EACCES: // erro de permissão (em relação ao processo que chama a função) // possivelmente redundante
				free(full_path);
				return 403;
			default:
				free(full_path);
				perror("(trata_gethead) Erro em <stat>");
				exit(errno);
		}
	}

	if (!(statinfo.st_mode & S_IRUSR)) { // se não há permissão de leitura para owner: forbidden
		free(full_path);
		return 403;
	}

	switch(statinfo.st_mode & S_IFMT) { // testa tipo de arquivo
		 // buf está definido em todo o escopo do switch
		
		case S_IFREG: // arquivo regular
			entrega_recurso(full_path, statinfo, connection_type, req_code, saidafd, registrofd);
			free(full_path);
			return 0;

		case S_IFDIR: // diretório
			int dirfd;
			struct stat file_statinfo; // statinfo se refere a diretório

			if (!(statinfo.st_mode & S_IXUSR)) { // se não há permissão de execução para usuário: forbidden	
				free(full_path);
				return 403;
			}

			if ((dirfd = open(full_path, O_RDONLY)) == -1) { // abre diretório
				free(full_path);
				perror("(trata_gethead) Erro em open");
				exit(errno);
			}
			
			// REFACTORING: trocar ifs abaixo por função

			if (fstatat(dirfd, "index.html", &file_statinfo, 0) == 0) { // se existe index.html
				if (!(file_statinfo.st_mode & S_IRUSR)) { // se index.html não tem permissão de leitura: forbidden
					free(full_path);
					return 403;
				}
				// caso contrário: open, read, write
				char *fuller_path = concatena(full_path, "/index.html"); // concatena /index.html ao final da string
				free(full_path);
				entrega_recurso(fuller_path, file_statinfo, connection_type, req_code, saidafd, registrofd);
				free(fuller_path);
				return 0;
			}
			
			if (fstatat(dirfd, "welcome.html", &file_statinfo, 0) == 0) { // se existe welcome.html
				if (!(file_statinfo.st_mode & S_IRUSR)) { // se welcome.html não tem permissão de leitura: forbidden
					free(full_path);
					return 403;
				}
				// caso contrário: open, read, write
				char *fuller_path = concatena(full_path, "/welcome.html");
				free(full_path);
				entrega_recurso(fuller_path, file_statinfo, connection_type, req_code, saidafd, registrofd);
				free(fuller_path);
				return 0;
			}
			
			free(full_path);
			return 404; // não existe index.html nem welcome.html
	}
}

static void trata_options(const char *connection_type, int saidafd, int registrofd) {
	/*responde requisição com cabeçalho simples contendo "Allow:"*/

	char buf[MAXSIZE];
	char datebuf[64]; // guarda data atual formatada (sem \r\n)

	get_date(datebuf, sizeof(datebuf));

	int i = sprintf( // preenche buf com cabeçalho
		buf,
		"HTTP/1.1 204 No Content\r\nAllow: OPTIONS, GET, HEAD, TRACE\r\nConnection: %s\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\n\r\n",
		connection_type,
		datebuf
	);

	if (write(saidafd, buf, i) == -1) {
		perror("(trata_options) Erro em write");
		exit(errno);
	}

	registra_head(buf, registrofd);
}

static void trata_trace(const char *request, const char *connection_type, int saidafd, int registrofd) {
	/*responde requisição com pequeno cabeçalho e uma cópia dela*/

	char buf[MAXSIZE];

	build_head_err_trace(buf, 200, strlen(request), connection_type); // preenche buf com cabeçalho

	registra_head(buf, registrofd); // registra cabeçalho em registro.txt antes de requisição ser concatenada a buf

	strcat(buf, request); // concatena cabeçalho e request

	if (write(saidafd, buf, strlen(buf)) == -1) {
		perror("(trata_trace) Erro em write");
		exit(errno);
	}

}

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd) {
	/*resposta a requisição mal sucedida*/

	char buf[MAXSIZE];

	size_t size;
	char *msg;

	switch (status) {
		case 400:
			size = strlen(e400);
			msg = e400;
			break;
		case 403:
			size = strlen(e403);
			msg = e403;
			break;
		case 404:
			size = strlen(e404);
			msg = e404;
			break;
		case 503:
			size = strlen(e503);
			msg = e503;
			break;
		default:
			perror("(trata_erro) Erro em status code");
			exit(3);
	}

	int off = build_head_err_trace(buf, status, size, connection_type);

	registra_head(buf, registrofd); // registra saída em registro.txt antes de ler html de erro

	int i = 0;

	if (req_code != HEAD) { // só preenche resposta com conteúdo caso requisição não seja HEAD.
		strcat(buf, msg);
		i = size;
	}
	
	if (write(saidafd, buf, i + off) == -1) {
		perror("(trata_erro) Erro em write");
		exit(errno);
	}
	
}

int process_request(const char *webspace, const char *request,  const char *req_type, const char *resource, const char *connection_type, int saidafd, int registrofd) {
	/* Chama função respectiva para tratar requisição */

	int result = 0; // guarda status code retornado por trata_gethead()
	int req_code = -1; // identifica tipo de requisição (começa inválido). Utilizado para não trabalhar com strings dentro de trata_gethead e erro

	if (strcmp(req_type, "GET") == 0) {
		req_code = GET;
		result = trata_gethead(webspace, resource, connection_type, req_code, saidafd, registrofd);
	}
	else if (strcmp(req_type, "HEAD") == 0) {
		req_code = HEAD;
		result = trata_gethead(webspace, resource, connection_type, req_code, saidafd, registrofd);
	}
	else if (strcmp(req_type, "OPTIONS") == 0) {
		req_code = OPTIONS;
		trata_options(connection_type, saidafd, registrofd);
	}
	else if (strcmp(req_type, "TRACE") == 0) {
		req_code = TRACE;
		trata_trace(request, connection_type, saidafd, registrofd);
	} else {
		trata_erro(400, connection_type, req_code, saidafd, registrofd); // servidor apenas reconhece 4 tipos acima. Devolve 400 caso requisição seja diferente
	}

	if (result != 0) { // imprime mensagem de erro caso houve algum em requisição GET ou HEAD (apenas cabeçalho)
		trata_erro(result, connection_type, req_code, saidafd, registrofd);
	}

	return result;
}