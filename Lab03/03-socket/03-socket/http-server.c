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

void* http_server(void* arg);
void* https_server(void* arg);
void* handle_http_request(void* arg);
void* handle_https_request(void* arg);
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
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("HTTP socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0){
        perror("HTTP bind failed");
        exit(1);
    }
    listen(sock, 128);
    
    while (1)
    {
        struct sockaddr_in caddr;
        socklen_t addrlen;
        int request = accept(sock, (struct sockaddr*)&caddr, &addrlen);
        if (request < 0){
            perror("HTTP accept failed");
            exit(1);
        }

        pthread_t http_new_thread;
        if (pthread_create(&http_new_thread, NULL, (void*)handle_http_request, (void*)&request) != 0){
            perror("HTTP handle thread creation failed");
            exit(1);
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
        exit(1);
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, "./keys/cnlab.prikey", SSL_FILETYPE_PEM) <= 0){
        perror("load prikey failed");
        exit(1);
    }

    int sock;
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        perror("HTTPS socket creation failed");
        exit(1);
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr) < 0)){
        perror("HTTPS bind failed");
        exit(1);
    }
    listen(sock, 10);
    
    while (1)
    {
        struct sockaddr_in caddr;
        socklen_t addrlen;
        int request = accept(sock, (struct sockaddr*)&caddr, &addrlen);
        if (request < 0){
            perror("HTTPS accept failed");
            exit(1);
        }

        SSL *ssl = SSL_new(ctx);
        SSL_set_fd(ssl, request);


        pthread_t https_new_thread;
        if (pthread_create(&https_new_thread, NULL, (void*)handle_https_request, (void*)&ssl) != 0){
            perror("HTTPS handle thread creation failed");
            exit(1);
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
    char* send_buff = (char*)malloc(6000 * sizeof(char));
    memset(recv_buff, 0, 2000);
    memset(send_buff, 0, 6000);

    int request_len = recv(request, recv_buff, 2000, 0);
    if (request_len < 0){
        printf("HTTP receive failed");
        exit(1);
    }

    char *get = strstr(recv_buff, "GET");
    if (get){
        char *pos = get + 4;
        char *url = (char*)malloc(50 * sizeof(char));
        char *http_version = (char*)malloc(9 * sizeof(char));
        char *host = (char*)malloc(100 * sizeof(char));
        int relative_url = *pos == '/';

        int i = 0;
        for (i = 0; *pos != ' '; pos++, i++)
            url[i] = *pos;
        url[i] = '\0';
        pos++;

        for (i = 0; *pos != '\r'; pos++, i++)
            http_version[i] = *pos;
        http_version[i] = '\0';

        if (relative_url){
            pos = strstr(recv_buff, "Host:");
            if (!pos){
                printf("HTTP host not found");
                exit(1);
            }
            pos += 6;

            for (int i = 0; *pos != '\r'; pos++, i++)
                host[i] = *pos;
            host[i] = '\0';
        }


        strcat(send_buff, host);
        strcat(send_buff, http_version);
        strcat(send_buff, " 301 Moved Permanently\r\nLocation");
        strcat(send_buff, "https://");

        if (relative_url){
            strcat(send_buff, host);
            strcat(send_buff, url);
        }
        else
            strcat(send_buff, &url[7]);
        strcat(send_buff, "\r\n\r\n\r\n\r\n");

        if (send(request, send_buff, strlen(send_buff), 0) < 0){
            printf("HTTP send failed");
            exit(1);
        }

        free(url);
        free(http_version);
        free(host);
    }
    free(send_buff);
    free(recv_buff);

    close(request);
    return NULL;
}

void* handle_https_request(void* arg){
    pthread_detach(pthread_self());
    SSL *ssl = (SSL*)arg;
    if (SSL_accept(ssl) == -1){
        printf("HTTPS SSL_accept fialed");
        exit(1);
    }

    char* recv_buff = (char*)malloc(2000 * sizeof(char));
    char* send_buff = (char*)malloc(6000 * sizeof(char));
    int keep_alive = 1;

    while (keep_alive){
        memset(recv_buff, 0, 2000);
        int request_len = SSL_read(ssl, recv_buff, 2000);
        if (request_len < 0){
            printf("HTTPS SSL_read failed");
            exit(1);
        }
        if (recv_buff[0] == '\0')
            break;

        char *url = (char*)malloc(50 * sizeof(char));
        char *http_version = (char*)malloc(9 * sizeof(char));
        char *path = (char*)malloc(100 * sizeof(char));
        char *get = strstr(recv_buff, "GET");

        if (get){
            char *pos = get + 4;
            int relative_url = *pos == '/'; 
            int range = 0;
            int range_begin, range_end;

            int i = 0;
            for (i = 0; *pos != ' '; pos++, i++)
                url[i] = *pos;
            url[i] = '\0';
            pos++;

            for (i = 0; *pos != '\r'; pos++, i++)
                http_version[i] = *pos;
            http_version[i] = '\0';

            if (pos = strstr(recv_buff, "Range:")){
                pos += 13;
                range = 1;
                range_begin = 0;

                while(*pos >= '0' && *pos <= '9'){
                    range_begin = range_begin * 10 + *pos - '0';
                    pos++;
                }
                pos++;

                if (*pos < '0' || *pos > '9')
                    range_end = -1;
                else{
                    range_end = 0;
                    while(*pos >= '0' && *pos <= '9'){
                        range_end = range_end * 10 + *pos - '0';
                        pos++;
                    }
                }
            }

            if (pos = strstr(recv_buff, "Connection:")){
                pos += 12;
                if (*pos == 'k')
                    keep_alive = 1;
                else
                    keep_alive = 0;
            }

            path[0] = '.';
            path[1] = '\0';
            if (relative_url)
                strcat(path, url);
            else{
                int count = 3;
                for (int i = 0; count > 0; i++){
                    if (url[i] == '/')
                        count--;
                }
                strcat(path, &url[i]);
            }


            FILE *fp = fopen(path, "r");
            if (!fp){
                memset(send_buff, 0, 6000);
                strcat(send_buff, http_version);
                strcat(send_buff, " 404 Not Found\r\n\r\n\r\n\r\n");
                SSL_write(ssl, send_buff, strlen(send_buff));
                break;
            }
            else{
                char header[200] = {0};
                strcat(header, http_version);
                if (range)
                    strcat(header, " 206 Partial Content\r\n");
                else
                    strcat(header, " 200 OK\r\n");

                int file_size, file_begin;
                if (range){
                    if(range_end == -1){
                        fseek(fp, 0L, SEEK_END);
                        file_size = ftell(fp) - range_begin + 1;
                        file_begin = range_begin;
                    }
                    else{
                        file_size = range_end - range_begin + 1;
                        file_begin = range_begin;
                    }
                }
                else{
                    fseek(fp, 0L, SEEK_END);
                    file_size = ftell(fp);
                    file_begin = 0;
                }

                strcat(header, "Content-Length: ");
                fseek(fp, file_begin, 0);
                char file_size_str[64] = {0};
                sprintf(file_size_str, "%d", file_size);

                char *response = (char*)malloc((file_size + 200) * sizeof(char));
                memset(response, 0, file_size + 200);
                strcat(response, header);
                strcat(response, file_size_str);

                strcat(response, "\r\nConnection: ");
                if (keep_alive)
                    strcat(response, "keep-alive");
                else
                    strcat(response, "close");
                
                strcat(response, "\r\n\r\n");
                fread(response + strlen(response), 1, file_size, fp);
                SSL_write(ssl, response, strlen(response));

                fclose(fp);
                if(range == 1 && range_end == -1)
                    break;
            }
        }
        free(url);
        free(http_version);
        free(path);
    }
    free(recv_buff);
    free(send_buff);

    SSL_free(ssl);
    close(SSL_get_fd(ssl));
    return NULL;
}