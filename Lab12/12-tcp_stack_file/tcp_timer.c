#include "tcp.h"
#include "tcp_timer.h"
#include "tcp_sock.h"
#include "log.h"

#include <stdio.h>
#include <unistd.h>
#include <pthread.h>

#define TIMER_TYPE_TIME_WAIT 0
#define TIMER_TYPE_RETRANS 1

static struct list_head timer_list;
static pthread_mutex_t timer_list_lock = PTHREAD_MUTEX_INITIALIZER;

// scan the timer_list, find the tcp sock which stays for at 2*MSL, release it
void tcp_scan_timer_list()
{
	// fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	pthread_mutex_lock(&timer_list_lock);

	struct tcp_timer *timer_p = NULL, *timer_q = NULL;
	list_for_each_entry_safe(timer_p, timer_q, &timer_list, list) {
		if (timer_p->enable) {
			timer_p->timeout -= TCP_TIMER_SCAN_INTERVAL;
			if (timer_p->timeout <= 0) {
				struct tcp_sock *tsk = NULL;
				if (timer_p->type == TIMER_TYPE_TIME_WAIT) {
					timer_p->enable = 0;
					tsk = timewait_to_tcp_sock(timer_p);
					tcp_set_state(tsk, TCP_CLOSED);
					tcp_unhash(tsk);
					tcp_bind_unhash(tsk);
					list_delete_entry(&timer_p->list);
					free_tcp_sock(tsk);
				}
				else if (timer_p->type == TIMER_TYPE_RETRANS)
					tsk = retranstimer_to_tcp_sock(timer_p);
			}
		}
	}
	pthread_mutex_unlock(&timer_list_lock);
}

// set the timewait timer of a tcp sock, by adding the ti0.0mer into timer_list
void tcp_set_timewait_timer(struct tcp_sock *tsk)
{
	// fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	tsk->timewait.enable = 1;
	tsk->timewait.type = TIMER_TYPE_TIME_WAIT;
	tsk->timewait.timeout = TCP_TIMEWAIT_TIMEOUT;

	tsk->ref_cnt += 1;
	log(DEBUG, "insert " IP_FMT ":%hu <-> " IP_FMT ":%hu to timewait, ref_cnt += 1", 
			HOST_IP_FMT_STR(tsk->sk_sip), tsk->sk_sport,
			HOST_IP_FMT_STR(tsk->sk_dip), tsk->sk_dport);
	pthread_mutex_lock(&timer_list_lock);
	list_add_tail(&tsk->timewait.list, &timer_list);
	pthread_mutex_unlock(&timer_list_lock);
}

// scan the timer_list periodically by calling tcp_scan_timer_list
void *tcp_timer_thread(void *arg)
{
	init_list_head(&timer_list);
	while (1) {
		usleep(TCP_TIMER_SCAN_INTERVAL);
		tcp_scan_timer_list();
	}

	return NULL;
}
