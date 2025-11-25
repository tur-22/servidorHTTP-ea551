#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

#include <sys/socket.h>
#include <sys/wait.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include "especifica.tab.h"
#include "include/parser.h"
#include "include/process.h"

#ifndef MAXSIZE
#define MAXSIZE 16384
#endif

long int curr; // numero atual de processos filho (global para ser acessado por callback)

void handle_sigchld() {
	/*Gemini*/
	int saved_errno = errno; // Salva o errno, pois waitpid pode alterá-lo

    // Usamos um loop com WNOHANG para "limpar" TODOS os filhos
    // zumbis que possam estar na fila, não apenas um.
    while (waitpid(-1, NULL, WNOHANG) > 0) {
		curr--; // Decrementa o contador para cada filho "limpo"
    }

    errno = saved_errno; // Restaura o errno
}

int main(int argc, char *argv[]) {

	if (argc != 4) { // verifica número de argumentos
		printf("Utilização: %s <N> <path absoluto webspace> <path registro>\n", argv[0]);
		return 1;
	}

	long int N = strtol(argv[1], NULL, 10);
	printf("Programa iniciado com N = %ld\n\n", N);

    int registrofd; // file descriptor para registro.txt
	if ((registrofd = open(argv[3], O_WRONLY | O_CREAT | O_APPEND, 0600)) == -1) { // file descriptor para registro.txt
		perror("Erro em open (main: registro_fd)");
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
		perror("Erro em setsockopt");
		exit(errno);
	}

	meu_servidor.sin_family = AF_INET;
	meu_servidor.sin_port = htons(3333); // htons() converte para representação de rede
	meu_servidor.sin_addr.s_addr = INADDR_ANY; // qualquer endereço válido

	bind(soquete, (struct sockaddr*)&meu_servidor, sizeof(meu_servidor));
	listen(soquete, N+1); // prepara socket e uma lista para receber até N+1 conexões (poderia ser outro valor, não depende de numero max de processos-filho)

	signal(SIGCHLD, handle_sigchld); // atribui tratador de SIGCHILD, que decrementa curr

	int pid = 1;

	while (pid) { // loop processo pai
		printf("Processo pai: Aguardando conexão... (curr = %ld)\n", curr);

		do {
			soquete_msg = accept(soquete, (struct sockaddr*)&meu_cliente, &tam_endereco);
		} while (soquete_msg == -1 && errno == EINTR);

		if (soquete_msg == -1) {
			perror("Erro em accept");
			exit(errno);
		}

		if (curr < N) {
			pid = fork(); // processo filho terá pid = 0 e sairá do loop
			if (pid) {
 				printf("Processo pai: Pedido recebido. Abrindo conexão a ser tratada por %d.\n", pid); // aparentemente navegador abre conexão imediatamente após uma ser fechada (pré-conexão)
				curr++;
				close(soquete_msg);
			}
		} else {
			printf("Processo pai: Servidor sobrecarregado!\n");
			trata_erro(503, "close", -1, soquete_msg, registrofd, NULL); // na prática, envia resposta como se fosse a uma requisição get
			close(soquete_msg);
		}
	}

	// De agora em diante: processo filho

	close(soquete);

	/* Declarações para poll */
	int s; // retorno de poll (número de fds disponíveis)
	int tolerancia = 8000; // ms
	struct pollfd pfd;
	pfd.fd = soquete_msg;
	pfd.events = POLLIN; // estou observando fd para leitura

	while (1) { // se processo filho, read-process-write até conexão ser fechada
		pid = getpid();

		char buf[MAXSIZE]; // guarda mensagem de requisição
		int i;

		pfd.revents = 0; // zera flags de eventos disponíveis retornados por poll

		s = poll(&pfd, 1, tolerancia);
		if (s > 0 && (pfd.revents & POLLIN)) {
			if ((i = read(soquete_msg, buf, sizeof(buf))) == -1) { 
				perror("Erro em read (main: socket)");
				exit(errno);
			}
			if (i == 0) {
                printf("Conexão fechada pelo cliente.\n");
                break;
            }
			printf("PID %d: Mensagem recebida.\n", pid);
		} else if(s == 0) { // nenhuma mensagem foi enviada no intervalo definido
			printf("PID %d: Nada foi enviado em %d ms. Fechando conexão.\n", pid, tolerancia);
			break;
		} else {
			perror("Erro em poll");
			exit(errno);
		}

		if (!(yyin = fmemopen(buf, i, "r"))) { // chatgpt (pensei em fazer com [fdopen, fseek e fread] ou [read, lseek e fdopen], mas não funciona para sockets)
			perror("Erro em abertura de arquivo de entrada");
			exit(errno);
		}

		/* Reseta estado global do parser e lexer para atender próxima requisição (Gemini) */ 
		yylineno = 1;
		yyrestart(yyin);
		reset_lexer_state();

		yyparse();

		fclose(yyin); // evita vazamento de memória pois fmemopen causa uma alocação de estrutura para gerenciar estado da stream

		if (!campos) { // Gemini: corrige erro de segfault quando yyparse termina sem chamar adiciona_campo() (campos == NULL)
			fprintf(stderr, "PID %d: Requisição vazia ou malformada recebida. Ignorando.\n", pid);
			break;          // Volta para o início do while(1) e espera a próxima
		}

		params p;

		p.req_type = campos->nome; // tipo de requisição
		p.resource = campos->valores->nome; // caminho para o recurso buscado

		char connection_default[] = "close"; // valor padrão
		int is_default = 0; // solução barata para ver se pode dar free em connection_type

		if (!busca_connection_type(campos, &(p.connection_type))) {
			p.connection_type = connection_default;
			is_default = 1;
		}

		if (!busca_auth(campos, &(p.auth))) {
			p.auth = NULL;
		}

		if (write(registrofd, buf, i) == -1) { // escreve requisição em registro.txt
			perror("Erro em write");
			exit(errno);
		}
		
		process_request(argv[2], buf, p, soquete_msg, registrofd);

		imprime_campos(campos);

		destroi_campos();

		if (p.auth)
			free(p.auth);

		if (strcmp(p.connection_type, "close") == 0) {
			if (!is_default)
				free(p.connection_type);
			printf("PID %d: Conexão do tipo close fechada.\n", pid);
			break;
		}	

		free(p.connection_type);
	}

	close(soquete_msg);
	
	printf("PID %d: Fim do processo.\n\n", pid);
	return 0;
}