#ifndef __TCP_APPS_H__
#define __TCP_APPS_H__

#define LOG_AS_PPT

void *tcp_server(void *arg);
void *tcp_client(void *arg);
void *tcp_server_file(void *arg);
void *tcp_client_file(void *arg);
#endif
