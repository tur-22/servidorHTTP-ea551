/*Interface para acesso a funções não estáticas de process.c e T.O. de parâmetros de requisição*/

typedef struct params {
    char *req_type;
    char *resource;
    char *connection_type;
    char *auth;
} params;

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd, const char *realm);
int process_request(const char *webspace, const char *request, const params p, int saidafd, int registrofd);