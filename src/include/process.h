/*Interface para acesso a funções não estáticas de process.c*/

void trata_erro(int status, const char *connection_type, int req_code, int saidafd, int registrofd);
int process_request(const char *webspace, const char *request,  const char *req_type, const char *resource, const char *connection_type, int saidafd, int registrofd);