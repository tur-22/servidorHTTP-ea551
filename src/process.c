#include <stdio.h>
#include <stdlib.h>	
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <libgen.h>
#include <crypt.h>

#include <sys/stat.h>
#include <sys/time.h>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>

#include "include/process.h"

#define GET 0
#define HEAD 1
#define OPTIONS 2
#define TRACE 3

#ifndef MAXSIZE
#define MAXSIZE 16384
#endif

static const char * e400 = "<!DOCTYPE html><html><head><title>400 Bad Request</title></head><body><h1>400 Bad Request</h1><p>Aqui est&aacute; o conte&uacute;do de 400.html.</p></body></html>";
static const char * e401 = "<!DOCTYPE html><html><head><title>401 Unauthorized</title></head><body><h1>401 Unauthorized</h1><p>Aqui est&aacute; o conte&uacute;do de 401.html.</p></body></html>";
static const char * e403 = "<!DOCTYPE html><html><head><title>403 Forbidden</title></head><body><h1>403 Forbidden</h1><p>Aqui est&aacute; o conte&uacute;do de 403.html.</p></body></html>";
static const char * e404 = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>Aqui est&aacute; o conte&uacute;do de 404.html.</p></body></html>";
static const char * e503 = "<!DOCTYPE html><html><head><title>503 Service Unavailable</title></head><body><h1>503 Service Unavailable</h1><p>O servidor est&aacute; sobrecarregado. Tente novamente.</p></body></html>";

static unsigned char *base64_decode(const char *input, int length, int *out_len);
static char * concatena(const char *str1, const char *str2);
static char * simplifica_path(char * path);
static void get_date(char *buf, int bufsize);
static void registra_head(char *buf, int registrofd);
static int build_head(char *buf, int req_code, int status, const struct stat *statinfo, size_t content_length, const char *connection_type, const char *realm);
static void entrega_recurso_head(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd);
static void entrega_recurso_get(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd);
static void entrega_recurso(char * path, struct stat statinfo, const char *connection_type, int req_code, int saidafd, int registrofd);
static int trata_gethead(const char *webspace, params p, int req_code, int saidafd, int registrofd, char *realm);
static void trata_options(const char *connection_type, int saidafd, int registrofd);
static void trata_trace(const char *request, const char *connection_type, int saidafd, int registrofd);
static int busca_htaccess(const char *webspace, char *search_path);
static void sobe_busca(char *search_path);
static int busca_credenciais(const char *usuario, const char *hash, const char *htpath);
static int verifica_credenciais(const char *credenciais, const char *htpath);
static int autentica(const char *webspace, char *full_path, char *auth, char *realm);

/*Feito com IA*/
static unsigned char *base64_decode(const char *input, int length, int *out_len) {
    BIO *b64, *bmem;

    unsigned char *buffer = (unsigned char *)malloc(length);
    memset(buffer, 0, length);

    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new_mem_buf(input, length);
    bmem = BIO_push(b64, bmem);

    // Do not look for newlines (optional, depends on input format)
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); 

    *out_len = BIO_read(b64, buffer, length);

    BIO_free_all(b64);

    return buffer;
}

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

	int termina_em_barra = (path[len-1] == '/') ? 1 : 0;
	// se recurso buscado for '/', é necessário adicionar '/' no final da string caso o caminho do webspace também termine em '/'

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

	if (termina_em_barra) {
		strcat(s, "/");
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

static int build_head(char *buf, int req_code, int status, const struct stat *statinfo, size_t content_length, const char *connection_type, const char *realm) {
	/*Constrói cabeçalho de resposta apropriado para a requisição e o salva em buf*/

	char datebuf[64]; // guarda data atual formatada (sem \r\n)
	char statusmsg[32]; // guarda mensagem referente ao status code, a ser exibida na primeira linha da resposta
	char content_type[32];
	int off; // número de caracteres escritos em buf para armazenar header

	get_date(datebuf, sizeof(datebuf));

	if (req_code == OPTIONS) {

		off = sprintf(
			buf,
			"HTTP/1.1 204 No Content\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nAllow: OPTIONS, GET, HEAD, TRACE\r\n\r\n",
			datebuf,
			connection_type
		);

	} else if (status == 200 && req_code == GET || req_code == HEAD) {

		char modtbuf[64]; // guarda data de última modificação formatada (sem \r\n)

		strftime(modtbuf, sizeof(modtbuf), "%c BRT", localtime(&(statinfo->st_mtim.tv_sec))); // localtime retorna struct com tempo de última modificação formatado
		off = sprintf(
			buf,
			"HTTP/1.1 200 OK\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nContent-Length: %ld\r\nContent-Type: text/html\r\nLast-Modified: %s\r\n\r\n",
			datebuf,
			connection_type,
			statinfo->st_size,
			modtbuf
		);
		
	} else if (status == 401) {

		off = sprintf(
			buf,
			"HTTP/1.1 401 Unauthorized\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nWWW-Authenticate: Basic realm=\"%s\"\r\nContent-Length: %ld\r\nContent-Type: text/html\r\n\r\n",
			datebuf,
			connection_type,
			realm,
			content_length
		);

	} else { // demais erros ou trace

		switch(status) {
			case 200: // TRACE
				sprintf(statusmsg, "200 OK");
				sprintf(content_type, "message/http");
				break;
			case 400:
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
	}	

	return off;

	/*operações de formatação de data feitas com ajuda do chatgpt*/
}

static void entrega_recurso_head(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd) {
	/* constrói buffer com cabeçalho e o escreve na saída */

	char buf[MAXSIZE];
	int i = build_head(buf, HEAD, 200, &statinfo, 0, connection_type, NULL);

	if (write(saidafd, buf, i) == -1) {
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

	off = build_head(buf, GET, 200, &statinfo, 0, connection_type, NULL);

	registra_head(buf, registrofd); // registra saída em registro.txt antes de ler recurso

	if ((fd = open(path, O_RDONLY)) == -1) {
		perror("(entrega_recurso_get) Erro em open (entrega_recurso_get)");
		exit(errno);
	}
	if ((i = read(fd, buf + off, sizeof(buf) - off)) == -1) {
		perror("(entrega_recurso_get) Erro em read");
		exit(errno);
	}
	if (write(saidafd, buf, i + off) == -1) {
		perror("(entrega_recurso_get) Erro em write");
		exit(errno);
	}
	close(fd);
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

static void sobe_busca(char *search_path) {
	/*modifica search_path para conter path de possível .htaccess no diretório acima*/
	dirname(search_path);
	dirname(search_path);
	strcat(search_path, "/.htaccess");
}

static int busca_htaccess(const char *webspace, char *search_path) {
	/*sobe árvore de diretórios até encontrar (ou não) um .htaccess existente (modifica search_path)*/
	int len_webspace = strlen(webspace);
	int sucesso = 0;

	do {
		if (access(search_path, F_OK) == 0) {
			sucesso = 1; // existe htaccess em search_path
			break;
		}
		sobe_busca(search_path);
	} while(!strncmp(webspace, search_path, len_webspace));

	return sucesso;
}

static int busca_credenciais(const char *usuario, const char *senha, const char *htpath) {
	/*Itera sobre arquivo de senhas buscando informações de login*/

	FILE *fp;
	if (!(fp = fopen(htpath, "r"))) { // abre .htaccess
		perror("(autentica) Erro em fopen");
		exit(errno);
	}	
	
	char linha[512];

	char *saveptr; // para strtok
	char *usuario_arquivo;
	char *senha_arquivo;

	struct crypt_data data; // para crypt
    data.initialized = 0;

	while (fgets(linha, sizeof(linha), fp)) {
		usuario_arquivo = strtok_r(linha, ":\n", &saveptr);
		senha_arquivo = strtok_r(NULL, ":\n", &saveptr);

		char salt[21];

		strncpy(salt, senha_arquivo, 20);
		salt[20] = '\0';	

		char *hash;

		if (strcmp(usuario, usuario_arquivo) == 0) {
			
			hash = crypt_r(senha, salt, &data); // thread safe

			if (!hash || hash[0] == '*') { 
				perror("Erro em crypt"); // cai aqui caso não seja inserida senha, por exemplo
				return 0;
			}

			if (strcmp(hash, senha_arquivo) == 0) {
				fclose(fp);
				return 1;
			}
		}
	}

	fclose(fp);
	return 0;
}

static int verifica_credenciais(const char *credenciais, const char *htpath) {
	/*Verifica se credenciais estão presentes em arquivo de senhas com path htpath*/

	char *credenciais_cpy = strdup(credenciais);

	char *saveptr;
	char *usuario = strtok_r(credenciais_cpy, ":", &saveptr);
	char *senha = strtok_r(NULL, ":", &saveptr);

	int autenticado = busca_credenciais(usuario, senha, htpath);
	
	free(credenciais_cpy);
	return autenticado;
}

static int autentica(const char *webspace, char *full_path, char *auth, char *realm) {

	int len = strlen(full_path);
	int termina_em_barra = full_path[len-1] == '/';
	char *search_path;

	// começa supondo que full_path representa diretório e buscando .htaccess dentro dele (para index.html)
	switch (termina_em_barra) {
		case 0:
			search_path = concatena(full_path, "/.htaccess");
			break;
		case 1:
			search_path = concatena(full_path, ".htaccess");
	}

	int htaccess = busca_htaccess(webspace, search_path);

	if (htaccess) {
		FILE *fp;
		if (!(fp = fopen(search_path, "r"))) { // abre .htaccess
			perror("(autentica) Erro em fopen");
			exit(errno);
		}

		// supondo que .htaccess possui formato [path .htpassword] + \n + [realm]
		char htpath[MAXSIZE];
		fgets(htpath, sizeof(htpath), fp);
		fgets(realm, sizeof(realm), fp);
		fclose(fp);

		len = strlen(htpath);
		if (len)
			htpath[len-1] = '\0'; // remove \n do final do path

		if (auth == NULL || strncmp(auth, "Basic", 5)) {
			printf("Campo de autorização válido requerido.\n\n");
			free(search_path);
			return 0; // não autenticado
		}

		char *credenciaisb64 = strndup(auth+5, strlen(auth+5)); // desconsidera "Basic"
		int out_len;
		char *credenciais = base64_decode(credenciaisb64, strlen(credenciaisb64), &out_len);
		free(credenciaisb64);

		printf("Credenciais em claro: %s\n", credenciais);

		if (!verifica_credenciais(credenciais, htpath)) {
			free(credenciais);
			printf("Falha na autenticação.\n\n");
			return 0; // não autenticado
		}

		free(credenciais);
		printf("Autenticação bem sucedida.\n\n");
	}

	free(search_path);
	return 1; // autenticado
}

static int trata_gethead(const char *webspace, const params p, int req_code, int saidafd, int registrofd, char *realm) {
	/*responde requisição get ou head, ou retorna status code de erro*/
	// req_code == 0: GET; req_code == 1: HEAD

	char *full_path = concatena(webspace, p.resource);
	full_path = simplifica_path(full_path); // simplifica path antes de verificar se inicia com path do webspace
	struct stat statinfo;	

	if (strncmp(webspace, full_path, strlen(webspace))) { // verifica se full_path inicia com path do webspace
		/* TODO: '/' deveria ser incluído no fim de path antes da comparação. Ex: [...]/../meu-webspace2 */
		/* TODO: aceitar caminho de webspace relativo */
		free(full_path);
		return 403; // caso não inicie: forbidden
	}
	
	if (!autentica(webspace, full_path, p.auth, realm)) {
		free(full_path);
		return 401;
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
				perror("(trata_gethead) Erro em <stat>");
				exit(errno);
		}
	}

	if (!(statinfo.st_mode & S_IRUSR)) { // se não há permissão de leitura para owner: forbidden
		free(full_path);
		return 403;
	}

	switch(statinfo.st_mode & S_IFMT) { // testa tipo de arquivo
		
		case S_IFREG: // arquivo regular
			entrega_recurso(full_path, statinfo, p.connection_type, req_code, saidafd, registrofd);
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
				perror("(trata_gethead) Erro em open");
				exit(errno);
			}
			
			// REFACTORING: trocar ifs abaixo por função

			if (fstatat(dirfd, "index.html", &file_statinfo, 0) == 0) { // se existe index.html
				close(dirfd);
				if (!(file_statinfo.st_mode & S_IRUSR)) { // se index.html não tem permissão de leitura: forbidden
					free(full_path);
					return 403;
				}
				// caso contrário: open, read, write
				char *fuller_path = concatena(full_path, "/index.html"); // concatena /index.html ao final da string
				free(full_path);
				entrega_recurso(fuller_path, file_statinfo, p.connection_type, req_code, saidafd, registrofd);
				free(fuller_path);
				return 0;
			}
			
			if (fstatat(dirfd, "welcome.html", &file_statinfo, 0) == 0) { // se existe welcome.html
				close(dirfd);
				if (!(file_statinfo.st_mode & S_IRUSR)) { // se welcome.html não tem permissão de leitura: forbidden
					free(full_path);
					return 403;
				}
				// caso contrário: open, read, write
				char *fuller_path = concatena(full_path, "/welcome.html");
				free(full_path);
				entrega_recurso(fuller_path, file_statinfo, p.connection_type, req_code, saidafd, registrofd);
				free(fuller_path);
				return 0;
			}
			
			close(dirfd);
			free(full_path);
			return 404; // não existe index.html nem welcome.html
	}
}

static void trata_options(const char *connection_type, int saidafd, int registrofd) {
	/*responde requisição com cabeçalho simples contendo "Allow:"*/

	char buf[MAXSIZE];
	char datebuf[64]; // guarda data atual formatada (sem \r\n)

	build_head(buf, OPTIONS, 204, NULL, 0, connection_type, NULL);

	if (write(saidafd, buf, strlen(buf)) == -1) {
		perror("(trata_options) Erro em write");
		exit(errno);
	}

	registra_head(buf, registrofd);
}

static void trata_trace(const char *request, const char *connection_type, int saidafd, int registrofd) {
	/*responde requisição com pequeno cabeçalho e uma cópia dela*/

	char buf[MAXSIZE];

	build_head(buf, TRACE, 200, NULL, strlen(request), connection_type, NULL); // preenche buf com cabeçalho

	registra_head(buf, registrofd); // registra cabeçalho em registro.txt antes de requisição ser concatenada a buf

	strcat(buf, request); // concatena cabeçalho e request

	if (write(saidafd, buf, strlen(buf)) == -1) {
		perror("(trata_trace) Erro em write");
		exit(errno);
	}

}

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd, const char *realm) {
	/*resposta a requisição mal sucedida*/

	char buf[MAXSIZE];

	size_t size;
	const char *msg;

	switch (status) {
		case 400:
			size = strlen(e400);
			msg = e400;
			break;
		case 401:
			size = strlen(e401);
			msg = e401;
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

	int off = build_head(buf, req_code, status, NULL, size, connection_type, realm);

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

int process_request(const char *webspace, const char *request, const params p, int saidafd, int registrofd) {
	/* Chama função respectiva para tratar requisição */

	int result = 0; // guarda status code retornado por trata_gethead()
	int req_code = -1; // identifica tipo de requisição (começa inválido). Utilizado para não trabalhar com strings dentro de trata_gethead e erro
	char realm[256];

	if (strcmp(p.req_type, "GET") == 0) {
		req_code = GET;
		result = trata_gethead(webspace, p, req_code, saidafd, registrofd, realm);
	}
	else if (strcmp(p.req_type, "HEAD") == 0) {
		req_code = HEAD;
		result = trata_gethead(webspace, p, req_code, saidafd, registrofd, realm);
	}
	else if (strcmp(p.req_type, "OPTIONS") == 0) {
		req_code = OPTIONS;
		trata_options(p.connection_type, saidafd, registrofd);
	}
	else if (strcmp(p.req_type, "TRACE") == 0) {
		req_code = TRACE;
		trata_trace(request, p.connection_type, saidafd, registrofd);
	} else {
		trata_erro(400, p.connection_type, req_code, saidafd, registrofd, NULL); // servidor apenas reconhece 4 tipos acima. Devolve 400 caso requisição seja diferente
	}

	if (result != 0) { // imprime mensagem de erro caso houve algum em requisição GET ou HEAD (apenas cabeçalho)
		trata_erro(result, p.connection_type, req_code, saidafd, registrofd, realm);
	}

	return result;
}