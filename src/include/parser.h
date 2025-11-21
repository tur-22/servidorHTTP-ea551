/*Interface para main.c acessar declarações associadas ao parser*/

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

extern p_cnode campos;

void yyrestart(FILE *input_file);
void reset_lexer_state();
extern FILE *yyin;
extern int yylineno;

void destroi_campos();
void imprime_campos(p_cnode campos);
int busca_connection_type(p_cnode c, char **connection_type);
int busca_auth(p_cnode c, char **auth);