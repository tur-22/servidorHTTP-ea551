%{
	#include <stdio.h>
	#include <stdlib.h>
	#include <string.h>

	typedef struct vnode { // nó para guardar valor
		struct vnode *prox;
		char *nome;
	} vnode;

	typedef struct cnode { // nó para guardar campo
		struct cnode *prox;
		struct vnode *valores;
		char *nome;
	} cnode;

	typedef vnode * p_vnode;
	typedef cnode * p_cnode;

	int yylex(void);
	int yyerror(char const *s);
	extern int yylineno;
	
	p_cnode campos, iter_c; // aponta para primeiro e último campos
	p_vnode iter_v; // aponta para último valor do campo atual
	
	static void adiciona_campo(char *nome);
	static void adiciona_valor(char *nome);
	static char * concatena_valor(char *valor1, char *valor2); 
	static void destroi_valores(p_vnode lista);
	static void imprime_valores(p_vnode valores);
	
	void destroi_campos();
	void imprime_campos(p_cnode campos);
	int busca_connection_type(p_cnode c, char **connection_type);
	int busca_auth(p_cnode c, char **auth);
%}

%union {
	char *str;
}

%token DOIS_PONTOS
%token VIRGULA
%token NEWLINE
%token SP
%token <str> METODO
%token <str> PATH
%token <str> VERSAO
%token <str> NOME_C
%token <str> NOME_V
%type <str> valor

%destructor { free($$); } <str>

%%

entrada: linha_inicial linhas_header
		| linha_inicial
		| linhas_header { /* BAD REQUEST: método não especificado */ }
		;

linha_inicial: METODO SP PATH SP VERSAO SP NEWLINE { adiciona_campo($1); adiciona_valor($3); adiciona_valor($5); }
			 | METODO SP PATH SP VERSAO NEWLINE { adiciona_campo($1); adiciona_valor($3); adiciona_valor($5); }
			 ;

linhas_header: linhas_header linha_header
			 | linha_header
			 ;

linha_header: campo valores NEWLINE
			| campo NEWLINE
			| error NEWLINE	{ yyerrok; /*fprintf(stderr, "Erro na linha %d\n", yylineno-1); */ /* Ação movida para yyerror(). Agora yylineno se refere a linha do erro, uma vez que a mensagem é impressa imediatamente */ /* Erros na linha inicial também caem aqui */ }
			;

campo : NOME_C DOIS_PONTOS			{ adiciona_campo($1); } 
		;

valores : valores VIRGULA valor 	{ adiciona_valor($3); } // faltou tratar caso de VIRGULA VIRGULA e valores terminando em VIRGULA  
		| valor						{ adiciona_valor($1); } 
		;

valor : valor NOME_V				{ $$ = concatena_valor($1, $2); }
	  | NOME_V						{ $$ = $1; }
	  ;

%%

static void adiciona_campo(char *nome) {
	/* Cria nova entrada de campo e adiciona na última posição da lista */
	cnode *novo;
	if (!(novo = malloc(sizeof(cnode)))) {
		fprintf(stderr, "Erro de alocação de memória\n");
		exit(1);
	}
	novo->prox = NULL;
	novo->valores = NULL;
	novo->nome = nome;
	if (!campos) // primeira inserção
		campos = novo;
	else
		iter_c->prox = novo;
	iter_c = novo;
}

static void adiciona_valor(char *nome) {
	/* Cria nova entrada de valor e adiciona na última posição da lista do campo atual */
	vnode *novo;
	if (!(novo = malloc(sizeof(vnode)))) {
		fprintf(stderr, "Erro de alocação de memória\n");
		exit(1);
	}
	novo->prox = NULL;
	novo->nome = nome;
	if (!iter_c->valores)
		iter_c->valores = novo;
	else
		iter_v->prox = novo;
	iter_v = novo;
}

static char * concatena_valor(char *valor1, char *valor2) {
	char *ret;
	size_t len1 = strlen(valor1);
	size_t len2 = strlen(valor2);
	size_t total_len = len1 + len2 + 1; // comprimento total das strings + \0

	if (!(ret = malloc(total_len))) { // aloca string composta e verifica erro 
		fprintf(stderr, "Erro de alocação de memória\n");
		exit(1);
	}
	
	strcpy(ret, valor1); // copia e concatena strings
	strcat(ret, valor2);

	free(valor1); // libera memória dos tokens
	free(valor2);

	return ret;
	/*feito com ajuda do ChatGPT*/
}

static void destroi_valores(p_vnode lista) {
	/* Usado para limpar lista de valores */
	p_vnode aux;
	while (lista) {
		aux = lista->prox;
		free(lista->nome);
		free(lista);
		lista = aux;
	}
}

void destroi_campos() {
	/* Usado para limpar lista de campos */
	p_cnode aux;
	while (campos) {
		aux = campos->prox;
		free(campos->nome);
		destroi_valores(campos->valores);
		free(campos);
		campos = aux;
	}
}

void imprime_campos(p_cnode c) {
	/* Percorre lista de campos e imprime todos seguidos de seus respectivos valores */
	printf("----------Início imprime_campos----------\n");
	while (c) {
		printf("O campo %s possui os valores: ", c->nome);
		imprime_valores(c->valores);
		printf("\n");
		c = c->prox;
	}
	printf("----------Fim imprime_campos----------\n\n");
}

static void imprime_valores(p_vnode v) {
	/* Imprime todos os valores associados a um campo */
	while (v) {
		printf("%s", v->nome);
		if (v->prox)
			printf(", ");
		v = v->prox;
	}
}

int busca_connection_type(p_cnode c, char **connection_type) {
	/* Copia valor do campo Connection para connection_type, se houver. Retorna 1 em caso de sucesso, 0 caso contrário. */

	while (c) {
		if (strcmp(c->nome, "Connection") == 0) {
			*connection_type = strdup(c->valores->nome); // copia primeiro valor do campo com nome Connection para parâmetro
			return 1;
		}
		c = c->prox;
	}
	return 0;
}

int busca_auth(p_cnode c, char **auth) {
	/* Copia valor do campo Authorization para auth, se houver. Retorna 1 em caso de sucesso, 0 caso contrário. */

	while (c) {
		if (strcmp(c->nome, "Authorization") == 0) {
			*auth = strdup(c->valores->nome); // copia primeiro valor do campo com nome Authorization para parâmetro (e.g. BasicZWNvbXA6bW9kX0FC)
			return 1;
		}
		c = c->prox;
	}
	return 0;
}

int yyerror(char const *s) {
	fprintf(stderr, "Erro na linha %d\n", yylineno-1);
	return 0;
}
