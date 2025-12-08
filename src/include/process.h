/*Interface para acesso a funções não estáticas de process.c e T.O. de parâmetros de requisição*/

typedef struct params { // parâmetros de uso situacional a serem passados à rotina process_request
    char *webspace;
    char *request;
    char *req_msg;
    char *req_type;
    char *resource;
    char *connection_type;
    char *auth;
} params;

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd, const char *realm, const char *errmsg);
void process_request(const params p, int saidafd, int registrofd);