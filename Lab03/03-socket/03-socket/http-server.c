#include <errno.h>
#include <unistd.h>
#include <malloc.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <resolv.h>
#include <pthread.h>
#include <openssl/ssl.h>
#include <openssl/err.h>

int main(){
    pthread_t http, https;
    if (pthread_create(&http, NULL, http_server, NULL) != 0){
        perror("HTTP thread creation failed");
        return -1;
    }
    if (pthread_create(&https, NULL, http_server, NULL) != 0){
        perror("HTTPS thread creation failed");
        return -1;
    }
    pthread_join(http, NULL);
    pthread_join(https, NULL);
    return 0;
}

void* http_server(void* arg){
    int port = 80;
    int sock;
    if (sock = socket(AF_INET, SOCK_STREAM, 0) < 0){
        perror("HTTP socket creation failed");
        return -1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr) < 0)){
        perror("HTTP bind failed");
        return -1;
    }
    listen(sock, 128);
    
    while (1)
    {
        struct sockaddr_in caddr;
        socklen_t addrlen;
        int request = accept(sock, (struct sockaddr*)&caddr, &addrlen);
        if (request < 0){
            perror("HTTP accept failed");
            return -1;
        }

        pthread_t http_new_thread;
        if (pthread_create(&http_new_thread, NULL, (void*)handle_http_request, (void*)&request) != 0){
            perror("HTTP handle thread creation failed");
            return -1;
        }
    }

    close(sock);
    return NULL;
}

void* https_server(void* arg){
    int port = 443;
    SSL_library_init();
    OpenSSL_add_all_algorithms();
    SSL_load_error_strings();

    const SSL_METHOD *method = TLS_server_method();
    SSL_CTX *ctx = SSL_CTX_new(method);

    // load certificate and private key
    if (SSL_CTX_use_certificate_file(ctx, "./keys/cnlab.cert", SSL_FILETYPE_PEM) <= 0){
        perror("load cert failed");
        return -1;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM) <= 0){
        perror("load prikey failed")
        return -1;
    }

    int sock;
    if (sock = socket(AF_INET, SOCK_STREAM, 0) < 0){
        perror("HTTPS socket creation failed");
        return -1;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr) < 0)){
        perror("HTTPS bind failed");
        return -1;
    }
    listen(sock, 10);
    
    while (1)
    {
        struct sockaddr_in caddr;
        socklen_t addrlen;
        int request = accept(sock, (struct sockaddr*)&caddr, &addrlen);
        if (request < 0){
            perror("HTTPS accept failed");
            return -1;
        }

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, request);


        pthread_t https_new_thread;
        if (pthread_create(&https_new_thread, NULL, (void*)handle_https_request, (void*)&ssl) != 0){
            perror("HTTPS handle thread creation failed");
            return -1;
        }
    }

    close(sock);
    SSL_CTX_free(ctx);
    return NULL;
}

void* handle_http_request(void* arg){
    pthread_detach(pthread_self());
    int request = *(int*)arg;

    char* recv_buff = (char*)malloc(2000 * sizeof(char));
    char* send_buff = (char*)malloc(2000 * sizeof(char));
    memset(recv_buff, 0, 2000);

    int request_len = recv(request, recv_buff, 2000, 0);
    if (request_len < 0){
        printf("HTTP receive failed");
        return -1;
    }

    char *get = strstr(recv_buff, "GET");
    if (get){
        char *pos = get + 4;
        char *url = (char*)malloc(50 * sizeof(char));
        char *http_version = (char*)malloc(9 * sizeof(char));
        char *host = (char*)malloc(100 * sizeof(char));
        int relative_url = *pos == '/';

        for (int i = 0; *pos != )
    }
}