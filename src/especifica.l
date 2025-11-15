%{
	#include "especifica.tab.h"
	#include <stdlib.h>
%}

%option yylineno

%x leitura_path
%x leitura_versao
%x leitura_campos
%x leitura_valores
%x leitura_dois_pontos
%x pula_linha

COMENTARIO ^[#].*\n
NOME_CAMPO [a-zA-Z]+[a-zA-Z0-9-]*
NOME_VALOR (([(][^)\n\r]*[)])|(["][^\"\n\r]*["])|[^ \n\r\t\",]+)+
METODO [A-Z]+
PATH [/][^ \n\t\r]*
VERSAO [H][T][T][P][/][0-9][.][0-9]
FIM_LINHA ([\r][\n])|[\n]

%%

<*>{COMENTARIO}					;

<INITIAL>{METODO}				{ BEGIN leitura_path; yylval.str = strdup(yytext); return METODO; }
<INITIAL>[ \t\n\r]+				{ /* ignora espaços iniciais e linhas em branco */ /* a princípio, apenas ignora \r */}
<INITIAL>.						{ BEGIN pula_linha; printf("Erro linha %d: nome de método inválido.\n", yylineno); /* Primeiro caractere não branco inválido. */  }

<leitura_path>{PATH} 			{ BEGIN leitura_versao; yylval.str = strdup(yytext); return PATH; }
<leitura_path>[ \t]+		    { return SP; }
<leitura_path>{FIM_LINHA}		{ BEGIN leitura_campos; printf("Erro linha %d: versão e path não fornecidos.\n", yylineno-1); return NEWLINE; }
<leitura_path>.					{ BEGIN pula_linha; printf("Erro linha %d: path não começa com '/'.\n", yylineno); }

<leitura_versao>{VERSAO}		{ yylval.str = strdup(yytext); return VERSAO; }
<leitura_versao>[ \t]+			{ return SP; }
<leitura_versao>{FIM_LINHA}		{ BEGIN leitura_campos; return NEWLINE; }
<leitura_versao>.				{ BEGIN pula_linha; printf("Warning linha %d: nome de versão inválido ou final da linha ignorado.\n", yylineno); /* pode ser que nome inválido venha após nome válido. Nesse caso, final da linha será ignorado. */ }

<leitura_campos>{NOME_CAMPO}	{ BEGIN leitura_dois_pontos; yylval.str = strdup(yytext); return NOME_C; }
<leitura_campos>[ \t]			;
<leitura_campos>{FIM_LINHA} 	{ /* Ignora linha vazia */ }
<leitura_campos>[:]				{ BEGIN pula_linha; printf("Erro linha %d: campo não especificado.\n", yylineno); /* Dois pontos antes de qualquer caractere não branco. */ } 
<leitura_campos>.				{ BEGIN pula_linha; printf("Erro linha %d: nome de campo inválido.\n", yylineno); /* Primeiro caractere não branco inválido. */ }

<leitura_dois_pontos>[:]		{ BEGIN leitura_valores; return DOIS_PONTOS; }
<leitura_dois_pontos>[ \t] 		;
<leitura_dois_pontos>{FIM_LINHA} { BEGIN leitura_campos; printf("Erro linha %d: linha sem (:).\n", yylineno-1); return NEWLINE; /* \n antes de (:) */ }
<leitura_dois_pontos>.			{ BEGIN pula_linha; printf("Erro linha %d: nome de campo inválido.\n", yylineno); /* Caractere inválido antes de (:) */ }

<leitura_valores>{NOME_VALOR}	{ yylval.str = strdup(yytext); return NOME_V; }
<leitura_valores>[,]			{ return VIRGULA; }
<leitura_valores>[ \t\"]		;
<leitura_valores>{FIM_LINHA}	{ BEGIN leitura_campos; return NEWLINE; }

<pula_linha>.*[\n]				{ BEGIN leitura_campos; return NEWLINE; }
<pula_linha>.*					{ return NEWLINE; /* Sem \n: última linha */}

%%

void reset_lexer_state() {
	BEGIN INITIAL;
}

/*OBS: Vazamento de memória possivelmente resolvido com diretiva %destructor do bison*/