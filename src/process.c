#include <stdio.h>
#include <stdlib.h>	
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <libgen.h>
#include <crypt.h>
#include <ctype.h>
#include <pthread.h>

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
#define POST 4

#ifndef MAXSIZE
#define MAXSIZE 16384
#endif

static const char * m200post = "<!DOCTYPE html><html><head><title>200 OK</title></head><body><h1>200 OK</h1><p>Senha alterada com sucesso.</p></body></html>";
static const char * e400 = "<!DOCTYPE html><html><head><title>400 Bad Request</title></head><body><h1>400 Bad Request</h1><p>Aqui est&aacute; o conte&uacute;do de 400.html.</p></body></html>";
static const char * e400novasenha = "<!DOCTYPE html><html><head><title>400 Bad Request</title></head><body><h1>400 Bad Request</h1><p>Os dois campos de nova senha n&atilde;o coincidem.</p></body></html>";
static const char * e400login = "<!DOCTYPE html><html><head><title>400 Bad Request</title></head><body><h1>400 Bad Request</h1><p>Usu&aacute;rio e/ou senha n&atilde;o conferem.</p></body></html>";
static const char * e401 = "<!DOCTYPE html><html><head><title>401 Unauthorized</title></head><body><h1>401 Unauthorized</h1><p>Aqui est&aacute; o conte&uacute;do de 401.html.</p></body></html>";
static const char * e403 = "<!DOCTYPE html><html><head><title>403 Forbidden</title></head><body><h1>403 Forbidden</h1><p>Aqui est&aacute; o conte&uacute;do de 403.html.</p></body></html>";
static const char * e404 = "<!DOCTYPE html><html><head><title>404 Not Found</title></head><body><h1>404 Not Found</h1><p>Aqui est&aacute; o conte&uacute;do de 404.html.</p></body></html>";
static const char * e500 = "<!DOCTYPE html><html><head><title>500 Internal Server Error</title></head><body><h1>500 Internal Server Error</h1><p>Erro na leitura ou escrita do arquivo de senhas.</p></body></html>";
static const char * e503 = "<!DOCTYPE html><html><head><title>503 Service Unavailable</title></head><body><h1>503 Service Unavailable</h1><p>O servidor est&aacute; sobrecarregado. Tente novamente.</p></body></html>";

static const char * tabela_extensoes[] = {".html", ".txt", ".pdf", ".gif", ".tif", ".png", ".jpg"};

enum tipos {
	HTML,
	TXT,
	PDF,
	GIF,
	TIF,
	PNG,
	JPG,
	OUTRO
};

static unsigned char *base64_decode(const char *input, int length, int *out_len);
static char * concatena(const char *str1, const char *str2);
static char * simplifica_path(char * path);
static void get_date(char *buf, int bufsize);
static void registra_head(char *buf, int registrofd);
static int build_head(char *buf, int req_code, int status, const struct stat *statinfo, size_t content_length, const char *connection_type, const char *content_type, const char *realm);
static void entrega_recurso_head(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd);
static void entrega_recurso_get(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd);
static void entrega_recurso(char * path, struct stat statinfo, const char *connection_type, int req_code, int saidafd, int registrofd);
static int le_htaccess(const char *htapath, char *htppath, int read_realm, char *realm);
static void sobe_busca(char *search_path);
static int busca_htaccess(const char *webspace, char *search_path);
static int busca_credenciais(const char *usuario, const char *hash, const char *htpath);
static int verifica_credenciais(const char *credenciais, const char *htpath);
static int autentica(const char *webspace, char *full_path, char *auth, char *realm);
static int trata_gethead(const char *webspace, const char *resource, const char *connection_type, char *auth, int req_code, int saidafd, int registrofd, char *realm);
static void trata_options(const char *connection_type, int saidafd, int registrofd);
static void trata_trace(const char *request, const char *connection_type, int saidafd, int registrofd);
static int hex_to_int(char c);
static void url_decode(char *str);
static void interpreta_form(char *req_msg_cpy, char **nomeusuario, char **senhaatual, char **novasenha, char **confirmanovasenha);
static void gerar_salt_thread_safe(char *salt);
static int altera_senha(const char *htppath, const char *nomeusuario, const char *senhaatual, const char *novasenha);
static int trata_post(const char *webspace, const char *resource, const char *req_msg, char *errmsg);

static pthread_rwlock_t htpasswd_lock = PTHREAD_RWLOCK_INITIALIZER; //GEMINI: lida com concorrência de leitura e escrita em arquivos de senhas

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
/*FIM IA*/

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

static int build_head(char *buf, int req_code, int status, const struct stat *statinfo, size_t content_length, const char *connection_type, const char *content_type, const char *realm) {
	/*Constrói cabeçalho de resposta apropriado para a requisição e o salva em buf*/

	char datebuf[64]; // guarda data atual formatada (sem \r\n)
	char statusmsg[32]; // guarda mensagem referente ao status code, a ser exibida na primeira linha da resposta
	int off; // número de caracteres escritos em buf para armazenar header

	get_date(datebuf, sizeof(datebuf));

	if (req_code == OPTIONS) {

		off = sprintf(
			buf,
			"HTTP/1.1 204 No Content\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nAllow: OPTIONS, GET, HEAD, TRACE, POST\r\n\r\n",
			datebuf,
			connection_type
		);

	} else if (status == 200 && req_code == GET || req_code == HEAD) {

		char modtbuf[64]; // guarda data de última modificação formatada (sem \r\n)

		strftime(modtbuf, sizeof(modtbuf), "%c BRT", localtime(&(statinfo->st_mtim.tv_sec))); // localtime retorna struct com tempo de última modificação formatado
		off = sprintf(
			buf,
			"HTTP/1.1 200 OK\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nContent-Length: %ld\r\nContent-Type: %s\r\nX-Content-Type-Options: nosniff\r\nLast-Modified: %s\r\n\r\n",
			datebuf,
			connection_type,
			statinfo->st_size,
			content_type,
			modtbuf
		);
		
	} else if (status == 200 && req_code == POST) {

		off = sprintf(
			buf,
			"HTTP/1.1 200 OK\r\nDate: %s\r\nServer: Servidor HTTP ver. 0.1 de Artur Paulos Pinheiro\r\nConnection: %s\r\nContent-Length: %ld\r\nContent-Type: text/html\r\n\r\n",
			datebuf,
			connection_type,
			content_length
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

		char default_content_type[32];

		switch(status) {
			case 200: // TRACE
				sprintf(statusmsg, "200 OK");
				sprintf(default_content_type, "message/http");
				break;
			case 400:
				sprintf(statusmsg, "400 Bad Request");
				sprintf(default_content_type, "text/html");
				break;
			case 403:
				sprintf(statusmsg, "403 Forbidden");
				sprintf(default_content_type, "text/html");
				break;
			case 404:
				sprintf(statusmsg, "404 Not Found");
				sprintf(default_content_type, "text/html");
				break;
			case 503:
				sprintf(statusmsg, "503 Service Unavailable");
				sprintf(default_content_type, "text/html");
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
			default_content_type
		);
	}	

	return off;

	/*operações de formatação de data feitas com ajuda do chatgpt*/
}

static void get_content_type(char *dest, const char *resource) {
	/*obtém formato de arquivo a partir de resource e escreve template correspondente em dest*/

	char * extensao = strrchr(resource, '.');
	enum tipos tipo;

	if (extensao) { // se arquivo possui extensão no nome
		int i;
		for (i = 0; i < sizeof(tabela_extensoes)/sizeof(tabela_extensoes[0]); i++) { // busca na tabela a partir do índice qual seu tipo (representado por enum)
			if (!strcmp(extensao, tabela_extensoes[i]))
				break;
		}
		tipo = i;
	} else {
		tipo = OUTRO;
	}

	switch (tipo) {
		case HTML:
			strcpy(dest, "text/html");
			break;
		case TXT:
			strcpy(dest, "text/plain");
			break;
		case PDF:
			strcpy(dest, "application/pdf");
			break;
		case GIF:
			strcpy(dest, "image/gif");
			break;
		case TIF:
			strcpy(dest, "image/tiff");
			break;
		case PNG:
			strcpy(dest, "image/png");
			break;
		case JPG:
			strcpy(dest, "image/jpeg");
			break;
		default:
			strcpy(dest, "application/octet-stream");
	}
}

static void entrega_recurso_head(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd) {
	/* constrói buffer com cabeçalho e o escreve na saída */

	char buf[MAXSIZE];
	char content_type[128];

	get_content_type(content_type, path); // path é passado em lugar de recurso pois final das duas strings é o mesmo
	int i = build_head(buf, HEAD, 200, &statinfo, 0, connection_type, content_type, NULL);

	if (write(saidafd, buf, i) == -1) {
		perror("(entrega_recurso_head) Erro em write");
		exit(errno);
	}

	registra_head(buf, registrofd);
}

static void entrega_recurso_get(char * path, struct stat statinfo, const char *connection_type, int saidafd, int registrofd) {
	/* constrói buffer com cabeçalho e recurso e o escreve na saída */
	
	int fd, i;
	int header_len; // offset a partir do qual read deve escrever em buf, devido ao cabeçalho
	char buf[MAXSIZE];
	char content_type[128];

	get_content_type(content_type, path);
	header_len = build_head(buf, GET, 200, &statinfo, 0, connection_type, content_type, NULL);

	registra_head(buf, registrofd); // registra saída em registro.txt antes de ler recurso

	if (write(saidafd, buf, header_len) == -1) {
		perror("(entrega_recurso_get) Erro em write");
		exit(errno);
	}

	if ((fd = open(path, O_RDONLY)) == -1) {
		perror("(entrega_recurso_get) Erro em open (entrega_recurso_get)");
		exit(errno);
	}

	do {
		if ((i = read(fd, buf, sizeof(buf))) == -1) {
			perror("(entrega_recurso_get) Erro em read");
			exit(errno);
		}
		if (write(saidafd, buf, i) == -1) {
			perror("(entrega_recurso_get) Erro em write");
			exit(errno);
		}
	} while (i == sizeof(buf));

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

static int le_htaccess(const char *htapath, char *htppath, int read_realm, char *realm) {
	/*extrai path de .htpassword e realm de .htaccess*/
	FILE *fp;
	if (!(fp = fopen(htapath, "r")))
		return 500; // erro em abertura de .htaccess

	fgets(htppath, MAXSIZE, fp);
	if (read_realm)
		fgets(realm, 256, fp);
	fclose(fp);

	int len = strlen(htppath);
	if (len)
		if (htppath[len-1] == '\n')
			htppath[len-1] = '\0'; // remove \n do final do path

	if (read_realm) {
		len = strlen(realm);
		if (len)
			if (realm[len-1] == '\n')
				realm[len-1] = '\0'; // remove \n do final de realm
	}
	return 0;
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

	pthread_rwlock_rdlock(&htpasswd_lock); // nenhuma thread pode escrever em .htpasswd enquanto uma thread está lendo

	FILE *fp;
	if (!(fp = fopen(htpath, "r"))) { // abre .htpassword
		perror("(busca_credenciais) Erro em fopen");
		pthread_rwlock_unlock(&htpasswd_lock);
		exit(errno);
	}	
	
	char linha[512];

	char *saveptr; // para strtok
	char *usuario_arquivo;
	char *senha_arquivo;

	struct crypt_data data; // para crypt
    data.initialized = 0;

	while (fgets(linha, sizeof(linha), fp)) {
		usuario_arquivo = strtok_r(linha, ":\n", &saveptr); // ignora \n pois é passado como separador
		senha_arquivo = strtok_r(NULL, ":\n", &saveptr);

		if (strcmp(usuario, usuario_arquivo) == 0) {
			
			char *hash = crypt_r(senha, senha_arquivo, &data); // thread safe

			if (!hash || hash[0] == '*') {  // hash null ou hash de erro (começa em *, conforme manual)
				fclose(fp);
				pthread_rwlock_unlock(&htpasswd_lock);
				perror("Erro em crypt"); // cai aqui caso não seja inserida senha, por exemplo
				return 0;
			}

			if (strcmp(hash, senha_arquivo) == 0) {
				fclose(fp);
				pthread_rwlock_unlock(&htpasswd_lock);
				return 1;
			}
		}
	}

	fclose(fp);
	pthread_rwlock_unlock(&htpasswd_lock);
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

	if (htaccess) { // htaccess encontrado
		char htppath[MAXSIZE]; // path de .htpassword

		int status = le_htaccess(search_path, htppath, 1, realm); 

		free(search_path);

		if (status) {
			/*erro em abertura de htaccess*/
			perror("(autentica) Erro em fopen"); 
			exit(errno);
		}

		if (auth == NULL || strncmp(auth, "Basic", 5)) {
			printf("Campo de autorização válido requerido.\n\n");
			return 0; // não autenticado
		}

		char *credenciaisb64 = strndup(auth+5, strlen(auth+5)); // desconsidera "Basic"
		int out_len;
		char *credenciais = base64_decode(credenciaisb64, strlen(credenciaisb64), &out_len);
		free(credenciaisb64);

		printf("Credenciais em claro: %s\n", credenciais);

		if (!verifica_credenciais(credenciais, htppath)) {
			free(credenciais);
			printf("Falha na autenticação.\n\n");
			return 0; // não autenticado
		}

		free(credenciais);
		printf("Autenticação bem sucedida.\n\n");
	}

	return 1; // autenticado
}

static int trata_gethead(const char *webspace, const char *resource, const char *connection_type, char *auth,
	 int req_code, int saidafd, int registrofd, char *realm) {
	/*responde requisição get ou head, ou retorna status code de erro*/
	// req_code == 0: GET; req_code == 1: HEAD

	char *full_path = concatena(webspace, resource);
	full_path = simplifica_path(full_path); // simplifica path antes de verificar se inicia com path do webspace
	struct stat statinfo;	

	if (strncmp(webspace, full_path, strlen(webspace))) { // verifica se full_path inicia com path do webspace
		/* TODO: '/' deveria ser incluído no fim de path antes da comparação. Ex: [...]/../meu-webspace2 */
		/* TODO: aceitar caminho de webspace relativo */
		free(full_path);
		return 403; // caso não inicie: forbidden
	}
	
	if (!autentica(webspace, full_path, auth, realm)) {
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
				perror("(trata_gethead) Erro em open");
				exit(errno);
			}
			
			// REFACTOR: trocar ifs abaixo por função

			if (fstatat(dirfd, "index.html", &file_statinfo, 0) == 0) { // se existe index.html
				if (!(file_statinfo.st_mode & S_IRUSR)) { // se index.html sem permissão de leitura

					/*SOLUÇÃO DE ÚLTIMA HORA (apenas dupliquei código abaixo): antes se index.html sem permissão: forbidden*/
					if (fstatat(dirfd, "welcome.html", &file_statinfo, 0) == 0) { // se existe welcome.html
						close(dirfd);
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

					close(dirfd);
					free(full_path);
					return 403;
				}
				close(dirfd);
				// caso contrário: open, read, write
				char *fuller_path = concatena(full_path, "/index.html"); // concatena /index.html ao final da string
				free(full_path);
				entrega_recurso(fuller_path, file_statinfo, connection_type, req_code, saidafd, registrofd);
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
				entrega_recurso(fuller_path, file_statinfo, connection_type, req_code, saidafd, registrofd);
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

	build_head(buf, OPTIONS, 204, NULL, 0, connection_type, NULL, NULL);

	if (write(saidafd, buf, strlen(buf)) == -1) {
		perror("(trata_options) Erro em write");
		exit(errno);
	}

	registra_head(buf, registrofd);
}

static void trata_trace(const char *request, const char *connection_type, int saidafd, int registrofd) {
	/*responde requisição com pequeno cabeçalho e uma cópia dela*/

	char buf[MAXSIZE];

	build_head(buf, TRACE, 200, NULL, strlen(request), connection_type, NULL, NULL); // preenche buf com cabeçalho

	registra_head(buf, registrofd); // registra cabeçalho em registro.txt antes de requisição ser concatenada a buf

	strcat(buf, request); // concatena cabeçalho e request

	if (write(saidafd, buf, strlen(buf)) == -1) {
		perror("(trata_trace) Erro em write");
		exit(errno);
	}

}

/*GEMINI: decodificação url-encoded*/
// Função auxiliar para converter caractere Hex para Inteiro
// Ex: 'A' -> 10, 'b' -> 11, '9' -> 9
static int hex_to_int(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

// Decodifica a string in-place (modifica a string original)
static void url_decode(char *str) {
    char *leitura = str;  // Ponteiro para ler a string codificada
    char *escrita = str;  // Ponteiro para escrever a versão decodificada

    while (*leitura) {
        if (*leitura == '+') {
            *escrita = ' ';
            leitura++;
        } 
        else if (*leitura == '%' && isxdigit(leitura[1]) && isxdigit(leitura[2])) {
            // Encontrou %XX. Converte os dois próximos chars hex para 1 char byte
            *escrita = (char)((hex_to_int(leitura[1]) << 4) | hex_to_int(leitura[2]));
            leitura += 3; // Pula o %, o dígito 1 e o dígito 2
        } 
        else {
            // Caractere normal, apenas copia
            *escrita = *leitura;
            leitura++;
        }
        escrita++;
    }
    *escrita = '\0'; // Finaliza a string decodificada
}
/*FIM GEMINI*/

static void interpreta_form(char *req_msg_cpy, char **nomeusuario, char **senhaatual, char **novasenha, char **confirmanovasenha) {
	/*Separa campos da mensagem da requisição post*/
	char *saveptr; //strtok

	strtok_r(req_msg_cpy, "=", &saveptr);
	*nomeusuario = strtok_r(NULL, "&", &saveptr);
	strtok_r(NULL, "=", &saveptr);
	*senhaatual = strtok_r(NULL, "&", &saveptr);
	strtok_r(NULL, "=", &saveptr);
	*novasenha = strtok_r(NULL, "&", &saveptr);
	strtok_r(NULL, "=", &saveptr);
	*confirmanovasenha = strtok_r(NULL, "&", &saveptr);

	url_decode(*nomeusuario);
	url_decode(*senhaatual);
	url_decode(*novasenha);
	url_decode(*confirmanovasenha);
}

static void gerar_salt_thread_safe(char *salt) {
	/*FEITO COM AJUDA DE IA*/
	/*Gera salt para ser usado na geração do hash da nova senha*/
    const char *charset = "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    
    // Cria uma semente única por thread/chamada misturando segundos e microssegundos
    // O XOR (^) ajuda a diferenciar chamadas muito próximas
    unsigned int seed = (unsigned int)(tv.tv_sec ^ tv.tv_usec);

    strcpy(salt, "$6$");
    int offset = 3;

    for (int i = 0; i < 16; i++) {
        // rand_r usa a semente local passada por referência
        int index = rand_r(&seed) % 64; 
        salt[offset + i] = charset[index];
    }
    
    salt[offset + 16] = '\0';
}

static int altera_senha(const char *htppath, const char *nomeusuario, const char *senhaatual, const char *novasenha) {
	/*Tenta modificar senha em .htpassword com base em formulario*/

	pthread_rwlock_wrlock(&htpasswd_lock); // nenhuma thread pode ler ou escrever em .htpasswd enquanto esta thread tenta alterar senha
	// (talvez seja possível melhorar o uso dessa trava (upgrade: read -> write?))

	FILE *fp, *fp_temp;
	char temp_path[MAXSIZE]; // GEMINI: escrever em segundo arquivo e então substituir original para evitar corrupção se servidor interrompido
    sprintf(temp_path, "%s.tmp", htppath);

	if (!(fp = fopen(htppath, "r"))) { // abre .htpassword
		pthread_rwlock_unlock(&htpasswd_lock);
		return 500; // erro em abertura de .htpassword
	}

	if (!(fp_temp = fopen(temp_path, "w"))) {
		fclose(fp);
		pthread_rwlock_unlock(&htpasswd_lock);
		return 500; // erro em abertura/criação de .htpasswd.tmp
	}

	char linha[512];
	char linha_copia[512]; // para contornar strtok_r modificando string

	char *saveptr; // para strtok
	char *usuario_arquivo;
	char *senha_arquivo;

	struct crypt_data data; // para crypt

	int senha_alterada = 0;

	while (fgets(linha, sizeof(linha), fp)) {
		strcpy(linha_copia, linha);

		usuario_arquivo = strtok_r(linha_copia, ":\n", &saveptr); // ignora \n pois é passado como separador
		senha_arquivo = strtok_r(NULL, ":\n", &saveptr);

		if (strcmp(nomeusuario, usuario_arquivo) == 0) {

			data.initialized = 0;	
			char *hash_teste = crypt_r(senhaatual, senha_arquivo, &data); // thread safe

			if (!hash_teste || hash_teste[0] == '*') {  // hash null ou hash de erro (começa em *, conforme manual)
				fclose(fp);
				fclose(fp_temp);
				pthread_rwlock_unlock(&htpasswd_lock);
				unlink(temp_path);
				perror("(altera_senha: senhaatual) Erro em crypt"); // cai aqui caso não seja inserida senha, por exemplo
				return 400; // erro em senhaatual, não será possível encontrar informações de login compatíveis
			}

			if (strcmp(hash_teste, senha_arquivo) == 0) {
				char salt[24];
				gerar_salt_thread_safe(salt);

				data.initialized = 0;
				char *novo_hash = crypt_r(novasenha, salt, &data);

				if (!novo_hash || novo_hash[0] == '*') { // hash null ou hash de erro
					fclose(fp);
					fclose(fp_temp);
					pthread_rwlock_unlock(&htpasswd_lock);
					unlink(temp_path);
					perror("(altera_senha: novasenha) Erro em crypt"); // cai aqui caso não seja inserida senha, por exemplo
					return 400; // erro em novasenha
				}

				fprintf(fp_temp, "%s:%s\n", nomeusuario, novo_hash); // escreve nova linha em arquivo temporário
				senha_alterada = 1;
			} else { // usuário encontrado, porém senha não bateu (pode haver outra entrada)
				fputs(linha, fp_temp);
			}
		} else { // linha não corresponde ao usuário: apenas copia
			fputs(linha, fp_temp);
		}
	}

	fclose(fp);
	fclose(fp_temp);

	int ret_value;

	if (senha_alterada) {
		if (rename(temp_path, htppath) != 0) {
            perror("(altera_senha) Erro ao renomear arquivo de senhas");
			ret_value = 500; // erro em "escrita" de .htpasswd
        } else
			ret_value = 200; // .htpasswd sobrescrito
	} else {
		ret_value = 400; // usuário não encontrado ou senha incorreta
	}
	
	unlink(temp_path);
	pthread_rwlock_unlock(&htpasswd_lock);
	return ret_value;
}

static int trata_post(const char *webspace, const char *resource, const char *req_msg, char *errmsg) {
	char *nomeusuario, *senhaatual, *novasenha, *confirmanovasenha;
	char *req_msg_cpy = strdup(req_msg); // cópia para strtok_r não modificar req_msg

	printf("Mensagem POST: %s\n", req_msg);

	interpreta_form(req_msg_cpy, &nomeusuario, &senhaatual, &novasenha, &confirmanovasenha);

	if (strcmp(novasenha, confirmanovasenha)) { // senhas novas não batem
		strcpy(errmsg, e400novasenha);
		free(req_msg_cpy);
		return 400;
	}

	char *aux_path = concatena(webspace, resource);
	dirname(aux_path);
	char *htapath = concatena(aux_path, "/.htaccess"); // obtém path de .htaccess
	free(aux_path);

	char htppath[MAXSIZE];
	int status = le_htaccess(htapath, htppath, 0, NULL); // obtém path de .htpassword

	free(htapath);

	if (status) {
		/*erro em abertura de .htaccess*/
		free(req_msg_cpy);
		return 500;
	}
	
	status = altera_senha(htppath, nomeusuario, senhaatual, novasenha);

	switch(status) {
		case 200: // gambiarra: usa trata_erro para tratar sucesso
			strcpy(errmsg, m200post);
			break;
		case 400:
			strcpy(errmsg, e400login);
			break;
		case 500:
			break;
		default:
			perror("(trata_post) Erro em altera senha");
			exit(4);
	}

	free(req_msg_cpy);
	return status;
}

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd, const char *realm, const char *errmsg) {
	/*resposta a requisição mal sucedida*/

	char buf[MAXSIZE];

	size_t size;
	const char *msg;

	if (!errmsg || !errmsg[0]) { // caso string de msg de erro não seja passada, usar padrão com base em status code.
		switch (status) {
			case 400:
				size = strlen(e400);
				msg = e400;
				break;
			case 401:
				size = strlen(e401);
				msg = e401;
				break;
			case 403:
				size = strlen(e403);
				msg = e403;
				break;
			case 404:
				size = strlen(e404);
				msg = e404;
				break;
			case 500:
				size = strlen(e500);
				msg = e500;
				break;
			case 503:
				size = strlen(e503);
				msg = e503;
				break;
			default:
				perror("(trata_erro) Erro em status code");
				exit(3);
		}
	} else {
		size = strlen(errmsg);
		msg = errmsg;
	}

	int off = build_head(buf, req_code, status, NULL, size, connection_type, NULL, realm);

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

void process_request(const params p, int saidafd, int registrofd) {
	/* Chama função respectiva para tratar requisição */

	int result = 0; // guarda status code retornado por trata_gethead()
	int req_code = -1; // identifica tipo de requisição (começa inválido). Utilizado para não trabalhar com strings dentro de trata_gethead e erro
	char realm[256];
	char errmsg[1024];
	errmsg[0] = '\0'; // serve como NULL em trata_erro

	if (strcmp(p.req_type, "GET") == 0) {
		req_code = GET;
		result = trata_gethead(p.webspace, p.resource, p.connection_type, p.auth, req_code, saidafd, registrofd, realm);
	}
	else if (strcmp(p.req_type, "HEAD") == 0) {
		req_code = HEAD;
		result = trata_gethead(p.webspace, p.resource, p.connection_type, p.auth, req_code, saidafd, registrofd, realm);
	}
	else if (strcmp(p.req_type, "OPTIONS") == 0) {
		req_code = OPTIONS;
		trata_options(p.connection_type, saidafd, registrofd);
	}
	else if (strcmp(p.req_type, "TRACE") == 0) {
		req_code = TRACE;
		trata_trace(p.request, p.connection_type, saidafd, registrofd);
	}
	else if (strcmp(p.req_type, "POST") == 0) {
		req_code = POST;
		result = trata_post(p.webspace, p.resource, p.req_msg, errmsg);
	} else {
		trata_erro(400, p.connection_type, req_code, saidafd, registrofd, NULL, NULL); // servidor apenas reconhece 5 tipos acima. Devolve 400 caso requisição seja diferente
	}

	if (result != 0) { // imprime mensagem de erro caso houve algum em requisição GET ou HEAD (apenas cabeçalho)
		trata_erro(result, p.connection_type, req_code, saidafd, registrofd, realm, errmsg);
	}
}