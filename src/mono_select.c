#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <arpa/inet.h>

#include "especifica.tab.h"
#include "include/parser.h"
#include "include/process.h"

#ifndef MAXSIZE
#define MAXSIZE 16384
#endif

int main(int argc, char *argv[]) {

	if (argc != 4) { // verifica número de argumentos
		printf("Utilização: %s <porta> <path absoluto webspace> registro.txt\n", argv[0]);
		return 1;
	}

	params p;
	p.webspace = argv[2];

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
	/* */

	uint16_t port = (uint16_t) strtol(argv[1], NULL, 10);
	meu_servidor.sin_family = AF_INET;
	meu_servidor.sin_port = htons(port); // htons() converte para representação de rede
	meu_servidor.sin_addr.s_addr = INADDR_ANY; // qualquer endereço válido

	bind(soquete, (struct sockaddr*)&meu_servidor, sizeof(meu_servidor));
	listen(soquete, 5); // prepara socket e uma lista para receber até 5 conexões

	int registrofd; // file descriptor para registro.txt
	if ((registrofd = open(argv[3], O_WRONLY | O_CREAT | O_APPEND, 0600)) == -1) {
		perror("Erro em open (main: registro_fd)");
		exit(errno);
	}

	/* Declarações para select */
	int n; // retorno de select (número de fds disponíveis)
	long int tolerancia = 8;
	fd_set fds;
	struct timeval timeout;

	while (1) {
		printf("Aguardando conexão...\n");

		// estou presumindo que servidor deve se manter escutando mesmo que nenhum pedido de conexão chegue em 8s. Estou usando select apenas em read (accept bloqueante, select apenas para fechar conexões).
		if ((soquete_msg = accept(soquete, (struct sockaddr*)&meu_cliente, &tam_endereco)) == -1) {
			perror("Erro em accept");
			exit(errno);
		}

		printf("Conexão aberta.\n"); // aparentemente navegador abre conexão imediatamente após uma ser fechada (pré-conexão)

		while(1) { // trabalha em conexão até ela ser fechada

			char buf[MAXSIZE]; // guarda mensagem de requisição
			int i;

			FD_ZERO(&fds); // limpa fd_set
			FD_SET(soquete_msg, &fds); // adiciona soquete_msg a fd_set
			timeout.tv_sec = tolerancia;
			timeout.tv_usec = 0;

			n = select(soquete_msg+1, &fds, (fd_set *) 0, (fd_set *) 0, &timeout); // verifica se soquete_msg está pronto para leitura
			if (n > 0 && FD_ISSET(soquete_msg, &fds)) {
				if ((i = read(soquete_msg, buf, sizeof(buf))) == -1) { 
					perror("Erro em read (main: socket)");
					exit(errno);
				}
				printf("Mensagem recebida.\n");
			} else if(n == 0) { // se n = 0, não existe fd em fds pronto para leitura -> nenhuma mensagem foi enviada em intervalo definido
				printf("Nada foi enviado em %ld s. Fechando conexão.\n\n", tolerancia);
				break;
			} else {
				perror("Erro em select");
				exit(errno);
			}

			p.request = buf;

			/*Separa cabeçalho da requisição de possível corpo*/
			char *header_end = strstr(buf, "\r\n\r\n");
			int header_len = header_end == NULL ? i : header_end - buf + 4; // encontra comprimento do cabeçalho da requisição

			p.req_msg = header_end == NULL ? header_end : header_end + 4; // mensagem após cabeçalho

			if (!(yyin = fmemopen(buf, header_len, "r"))) { // chatgpt (pensei em fazer com [fdopen, fseek e fread] ou [read, lseek e fdopen], mas não funciona para sockets)
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
				fprintf(stderr, "Requisição vazia ou malformada recebida. Ignorando.\n");
				break;          // Volta para o início do while externo e espera a próxima
			}

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
			
			process_request(p, soquete_msg, registrofd);

			imprime_campos(campos);

			destroi_campos();

			if (strcmp(p.connection_type, "close") == 0) {
				if (!is_default)
					free(p.connection_type);
				printf("Conexão do tipo close fechada.\n");
				break;
			}	
			fflush(stdout);

			if (p.auth)
				free(p.auth);

			free(p.connection_type);	
		}

		close(soquete_msg);
	}

	return 0;
}

// OBS: se cliente fecha o navegador (conexão), o programa recebe um request igual ao último e termina sua execução por erro ao tentar escrever no socket.