#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>

#include <sys/socket.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include "especifica.tab.h"
#include "include/parser.h"
#include "include/process.h"

#ifndef MAXSIZE
#define MAXSIZE 16384
#endif

typedef struct thread_args {
	int soquete_msg;
	char *webspace;
	int registrofd;
} thread_args;

pthread_mutex_t curr_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t processing_mutex = PTHREAD_MUTEX_INITIALIZER;

long int curr; // numero atual de threads

void * worker_thread(void * arg) {
	thread_args *args = (thread_args *)arg; // faz cast de arg para struct thread_args

	int soquete_msg = args->soquete_msg;
	int registrofd = args->registrofd;

	params p; // parâmetros de uso situacional a serem passados à rotina process_request

	p.webspace = args->webspace;

	/* Declarações para poll */
	int s; // retorno de poll (número de fds disponíveis)
	int tolerancia = 8000; // ms
	struct pollfd pfd;
	pfd.fd = soquete_msg;
	pfd.events = POLLIN; // estou observando fd para leitura

	while (1) { // read-process-write até conexão ser fechada

		char buf[MAXSIZE]; // guarda mensagem de requisição
		int i;

		pfd.revents = 0; // zera flags de eventos disponíveis retornados por poll

		s = poll(&pfd, 1, tolerancia);
		if (s > 0 && (pfd.revents & POLLIN)) {
			if ((i = read(soquete_msg, buf, sizeof(buf))) == -1) { 
				perror("(worker_thread) Erro em read");
				exit(errno);
			}
			if (i == 0) {
                printf("Worker thread (socket %d): Conexão fechada pelo cliente.\n", soquete_msg); //GEMINI
                break;
            }
			printf("Worker thread (socket %d): Mensagem recebida.\n", soquete_msg);
		} else if (s == 0) { // nenhuma mensagem foi enviada no intervalo definido
			printf("Worker thread (socket %d): Nada foi enviado em %d ms. Fechando conexão.\n", soquete_msg, tolerancia);
			break;
		} else {
			perror("(worker_thread) Erro em poll");
			exit(errno);
		}

		p.request = buf;

		/*Separa cabeçalho da requisição de possível corpo*/
		char *header_end = strstr(buf, "\r\n\r\n");
		int header_len = header_end == NULL ? i : header_end - buf + 4; // encontra comprimento do cabeçalho da requisição

		p.req_msg = header_end;

		pthread_mutex_lock(&processing_mutex); // região de processamento da mensagem (parser possui vários recursos globais)
		if (!(yyin = fmemopen(buf, header_len, "r"))) { // yyin contém apenas cabeçalho de requisição
			perror("(worker_thread) Erro em abertura de arquivo de entrada");
			exit(errno);
		}

		/* Reseta estado global do parser e lexer para atender próxima requisição (Gemini) */ 
		yylineno = 1;
		yyrestart(yyin);
		reset_lexer_state();

		yyparse();

		fclose(yyin); // evita vazamento de memória pois fmemopen causa uma alocação de estrutura para gerenciar estado da stream

		if (!campos) { // Gemini: corrige erro de segfault quando yyparse termina sem chamar adiciona_campo() (campos == NULL)
			pthread_mutex_unlock(&processing_mutex);
			fprintf(stderr, "Worker thread (socket %d): Requisição vazia ou malformada recebida. Ignorando.\n", soquete_msg);
			break;
		}

		p.req_type = strdup(campos->nome); // tipo de requisição
		p.resource = strdup(campos->valores->nome); // caminho para o recurso buscado
		/*strdup usado para poder destruir campos e liberar mutex logo*/

		char connection_default[] = "close"; // valor padrão
		int is_default = 0; // solução barata para ver se pode dar free em connection_type

		if (!busca_connection_type(campos, &(p.connection_type))) {
			p.connection_type = connection_default;
			is_default = 1;
		}

		if (!busca_auth(campos, &(p.auth)))
			p.auth = NULL;

		imprime_campos(campos);
		destroi_campos();
		pthread_mutex_unlock(&processing_mutex);

		if (write(registrofd, buf, i) == -1) { // escreve requisição em registro.txt
			perror("(worker_thread) Erro em write");
			exit(errno);
		}
		
		process_request(p, soquete_msg, registrofd);
		
		free(p.req_type);
		free(p.resource);
		if (p.auth)
			free(p.auth);

		if (strcmp(p.connection_type, "close") == 0) {
			if (!is_default)
				free(p.connection_type);
			printf("Worker thread (socket %d): Conexão do tipo close fechada.\n", soquete_msg);
			break;
		}
		
		free(p.connection_type);
	}

	close(soquete_msg);
	free(args);

	pthread_mutex_lock(&curr_mutex);
    curr--;
    pthread_mutex_unlock(&curr_mutex);

	printf("Worker thread (socket %d): Fim do trabalho.\n\n", soquete_msg);
    pthread_exit(NULL);
}

int main(int argc, char *argv[]) {

	if (argc != 4) { // verifica número de argumentos
		printf("Utilização: %s <N> <path absoluto webspace> <path registro>\n", argv[0]);
		return 1;
	}

	long int N = strtol(argv[1], NULL, 10);
	printf("Programa iniciado com N = %ld\n\n", N);

	int registrofd; // file descriptor para registro.txt
	if ((registrofd = open(argv[3], O_WRONLY | O_CREAT | O_APPEND, 0600)) == -1) {
		perror("(main) Erro em open");
		exit(errno);
	}

	int soquete, soquete_msg;
	struct sockaddr_in meu_servidor, meu_cliente;
	int tam_endereco = sizeof(meu_cliente);

	soquete = socket(AF_INET, SOCK_STREAM, 0);

	/* Gemini: SO_REUSEADDR permite testar programa imediatamente após fechar último teste com Ctrl+C */
	int opt = 1;
	// Permite que o soquete seja vinculado a um endereço que já está em uso (em estado TIME_WAIT)
	if (setsockopt(soquete, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
		perror("(main) Erro em setsockopt");
		exit(errno);
	}

	meu_servidor.sin_family = AF_INET;
	meu_servidor.sin_port = htons(3333); // htons() converte para representação de rede
	meu_servidor.sin_addr.s_addr = INADDR_ANY; // qualquer endereço válido

	bind(soquete, (struct sockaddr*)&meu_servidor, sizeof(meu_servidor));
	listen(soquete, N+1); // prepara socket e uma lista para receber até N+1 conexões (poderia ser outro valor, não depende de numero max de threads)

	pthread_t thread_id; // não preciso manter referência para threads ativas; uma variável pthread_t basta
	
	while (1) {
		printf("Thread principal: Aguardando conexão... (curr = %ld)\n", curr);

		do {
			soquete_msg = accept(soquete, (struct sockaddr*)&meu_cliente, &tam_endereco);
		} while (soquete_msg == -1 && errno == EINTR);

		if (soquete_msg == -1) {
			perror("(main) Erro em accept");
			exit(errno);
		}

		pthread_mutex_lock(&curr_mutex);
		if (curr < N) {
			curr++;
            pthread_mutex_unlock(&curr_mutex);

			thread_args *args = malloc(sizeof(thread_args));
			args->soquete_msg = soquete_msg;
			args->webspace = argv[2];
			args->registrofd = registrofd;
			
			if (pthread_create(&thread_id, NULL, worker_thread, (void *)args) != 0) {
				perror("(main) Erro em pthread_create");
				pthread_mutex_lock(&curr_mutex);
				curr--;
				pthread_mutex_unlock(&curr_mutex);
				close(soquete_msg);
			} else {
				pthread_detach(thread_id); // GEMINI: faz com que thread limpe seus recursos automaticamente ao terminar (não é necessário join)
				printf("Thread principal: Conexão aceita e passada para uma nova thread (socket %d).\n", soquete_msg);
			}

		} else {
			pthread_mutex_unlock(&curr_mutex);
			printf("Thread principal: Servidor sobrecarregado!\n");

			trata_erro(503, "close", -1, soquete_msg, registrofd, NULL); // na prática, envia resposta como se fosse a uma requisição get

			close(soquete_msg);
		}
	}

	// programa nunca chega aqui
	return 0;
}

/* ChatGPT/Gemini utilizado para parte da estruturação da seção de regras do bison, tratamento de erros no parser
	e gerenciamento de concorrência de threads */