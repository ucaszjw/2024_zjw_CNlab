#include "tcp_sock.h"

#include "log.h"

#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>

//char filename[100];

// return the interval in us
long get_interval(struct timeval tv_start,struct timeval tv_end){
    long start_us = tv_start.tv_sec * 1000000 + tv_start.tv_usec;
    long end_us   = tv_end.tv_sec   * 1000000 + tv_end.tv_usec;
    return end_us - start_us;
}

// tcp server application, listens to port (specified by arg) and serves only one
// connection request
void *tcp_server(void *arg)
{
	u16 port = *(u16 *)arg;
	struct tcp_sock *tsk = alloc_tcp_sock();

	struct sock_addr addr;
	addr.ip = htonl(0);
	addr.port = port;
	if (tcp_sock_bind(tsk, &addr) < 0) {
		log(ERROR, "tcp_sock bind to port %hu failed", ntohs(port));
		exit(1);
	}

	if (tcp_sock_listen(tsk, 3) < 0) {
		log(ERROR, "tcp_sock listen failed");
		exit(1);
	}

	log(DEBUG, "listen to port %hu.", ntohs(port));

	struct tcp_sock *csk = tcp_sock_accept(tsk);

	log(DEBUG, "accept a connection.");

	// sleep(5);
	char rbuf[1001], wbuf[1024];
	int rlen = 0;
	while (1) {
		rlen = tcp_sock_read(csk, rbuf, 1000);
		if (rlen == 0) {
			log(DEBUG, "tcp_sock_read return 0, the peer has closed.");
			break;
		} 
		else if (rlen > 0) {
			rbuf[rlen] = '\0';
			sprintf(wbuf, "server echoes: %s", rbuf);
			if (tcp_sock_write(csk, wbuf, strlen(wbuf)) < 0) {
				log(ERROR, "tcp_sock_write failed.");
				exit(1);
			}
		}
		else {
			log(ERROR, "tcp_sock_read return %d, this is an error.", rlen);
			exit(1);
		}
	}

	log(DEBUG, "close this connection.");

	tcp_sock_close(csk);
	
	return NULL;
}

// tcp client application, connects to server (ip:port specified by arg), each
// time sends one bulk of data and receives one bulk of data 
void *tcp_client(void *arg)
{
	struct sock_addr *skaddr = arg;

	struct tcp_sock *tsk = alloc_tcp_sock();

	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu) failed.", NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}

	// sleep(1);
	char *data = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	int len = strlen(data);
	char *wbuf = malloc(len + 1);
	char rbuf[1001];
	int rlen = 0;

	int n = 10;
	for (int i = 0; i < n; i++) {
		memcpy(wbuf, data+i, len-i);
		if (i > 0)
			memcpy(wbuf+len-i, data, i);

		int slen;
		if ((slen = tcp_sock_write(tsk, wbuf, len)) < 0) 
			break;

		rlen = tcp_sock_read(tsk, rbuf, 1000);
		if (rlen == 0) {
			log(DEBUG, "tcp_sock_read return 0, the peer has closed.");
			break;
		}
		else if (rlen > 0) {
			rbuf[rlen] = '\0';
			fprintf(stdout, "%s\n", rbuf);
		}
		else {
			log(ERROR, "tcp_sock_read return %d, this is an error.", rlen);
			exit(1);
		}

		sleep(1);
	}

	tcp_sock_close(tsk);

	free(wbuf);

	return NULL;
}

void *tcp_server_file(void *arg)
{
	FILE *fp = fopen("server-output.dat", "wb");
	if (fp == NULL) {
		log(ERROR, "open file server-output.dat failed.");
		exit(1);
	}
	log(DEBUG, "open file server-output.dat success.");

	struct timeval tv_start, tv_end;

	u16 port = *(u16 *)arg;
	struct sock_addr skaddr;
	struct tcp_sock *tsk = alloc_tcp_sock();
	skaddr.ip = htonl(0);
	skaddr.port = port;

	if (tcp_sock_bind(tsk, &skaddr) < 0) {
		log(ERROR, "tcp_sock bind to port %hu failed.", ntohs(port));
		exit(1);
	}
	if (tcp_sock_listen(tsk, 3) < 0) {
		log(ERROR, "tcp_sock listen failed.");
		exit(1);
	}
	log(DEBUG, "listening to port %hu.", ntohs(port));

	struct tcp_sock *csk = tcp_sock_accept(tsk);
	log(DEBUG, "accept a connection.");
	gettimeofday(&tv_start,NULL);

	char dbuf[20030];
	int dlen = 0;
	while (1) {
		dlen = tcp_sock_read(csk, dbuf, 20024);
		if (dlen > 0)
			fwrite(dbuf, 1, dlen, fp);
		else {
			log(DEBUG, "tcp_sock_read return %d, the peer has closed.", dlen);
			break;
		}
	}

	gettimeofday(&tv_end,NULL);
	long time_res = get_interval(tv_start,tv_end);
	time_res /= 1000000;
	fprintf(stderr, "used time: %ld s\n", time_res);

	log(DEBUG, "close this connection.");
	fclose(fp);
	tcp_sock_close(csk);
	
	return NULL;
}

void *tcp_client_file(void *arg)
{
	FILE *fp = fopen("client-input.dat", "rb");
	if (fp == NULL) {
		log(ERROR, "open file client-input.dat failed.");
		exit(1);
	}

	struct sock_addr *skaddr = arg;
	struct tcp_sock *tsk = alloc_tcp_sock();
	if (tcp_sock_connect(tsk, skaddr) < 0) {
		log(ERROR, "tcp_sock connect to server ("IP_FMT":%hu) failed.", NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));
		exit(1);
	}
	log(DEBUG, "connect to server ("IP_FMT":%hu) success.", NET_IP_FMT_STR(skaddr->ip), ntohs(skaddr->port));

	char dbuf[20030];
	int dlen = 0;
	int slen = 0;

	while (1) {
		dlen = fread(dbuf, 1, 20024, fp);
		if (dlen > 0) {
			slen += dlen;
			log(DEBUG, "send %d byte.", slen);
			tcp_sock_write(tsk, dbuf, dlen);
		}
		else {
			log(DEBUG, "file has sent done.");
			break;
		}
		usleep(10000);
	}

	fclose(fp);
	tcp_sock_close(tsk);

	return NULL;
}
