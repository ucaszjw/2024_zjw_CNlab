#include "tcp.h"
#include "tcp_hash.h"
#include "tcp_sock.h"
#include "tcp_timer.h"
#include "ip.h"
#include "rtable.h"
#include "log.h"

// TCP socks should be hashed into table for later lookup: Those which
// occupy a port (either by *bind* or *connect*) should be hashed into
// bind_table, those which listen for incoming connection request should be
// hashed into listen_table, and those of established connections should
// be hashed into established_table.

struct tcp_hash_table tcp_sock_table;
#define tcp_established_sock_table	tcp_sock_table.established_table
#define tcp_listen_sock_table		tcp_sock_table.listen_table
#define tcp_bind_sock_table			tcp_sock_table.bind_table

inline void tcp_set_state(struct tcp_sock *tsk, int state)
{
	/*
	log(DEBUG, IP_FMT":%hu switch state, from %s to %s.", \
			HOST_IP_FMT_STR(tsk->sk_sip), tsk->sk_sport, \
			tcp_state_str[tsk->state], tcp_state_str[state]);
	*/
	tsk->state = state;
}

// init tcp hash table and tcp timer
void init_tcp_stack()
{
	for (int i = 0; i < TCP_HASH_SIZE; i++)
		init_list_head(&tcp_established_sock_table[i]);

	for (int i = 0; i < TCP_HASH_SIZE; i++)
		init_list_head(&tcp_listen_sock_table[i]);

	for (int i = 0; i < TCP_HASH_SIZE; i++)
		init_list_head(&tcp_bind_sock_table[i]);

	pthread_t timer_wait, timer_retrans;
	pthread_create(&timer_wait, NULL, tcp_timer_thread, NULL);
	pthread_create(&timer_retrans, NULL, tcp_retrans_timer_thread, NULL);
}

// allocate tcp sock, and initialize all the variables that can be determined
// now
struct tcp_sock *alloc_tcp_sock()
{
	struct tcp_sock *tsk = malloc(sizeof(struct tcp_sock));

	memset(tsk, 0, sizeof(struct tcp_sock));

	tsk->state = TCP_CLOSED;
	tsk->rcv_wnd = TCP_DEFAULT_WINDOW;

	init_list_head(&tsk->list);
	init_list_head(&tsk->listen_queue);
	init_list_head(&tsk->accept_queue);
	init_list_head(&tsk->send_buf);
	init_list_head(&tsk->rcv_ofo_buf);

	tsk->rcv_buf = alloc_ring_buffer(tsk->rcv_wnd);
	pthread_mutex_init(&tsk->rcv_buf_lock, NULL);

	init_list_head(&tsk->send_buf);
	pthread_mutex_init(&tsk->send_buf_lock, NULL);

	tsk->wait_connect = alloc_wait_struct();
	tsk->wait_accept = alloc_wait_struct();
	tsk->wait_recv = alloc_wait_struct();
	tsk->wait_send = alloc_wait_struct();

	tsk->timewait.enable = 0;
	tsk->retrans_timer.enable = 0;

	log(DEBUG, "alloc a new tcp sock, ref_cnt = 1");
	tsk->ref_cnt += 1;

	return tsk;
}

// release all the resources of tcp sock
//
// To make the stack run safely, each time the tcp sock is refered (e.g. hashed), 
// the ref_cnt is increased by 1. each time free_tcp_sock is called, the ref_cnt
// is decreased by 1, and release the resources practically if ref_cnt is
// decreased to zero.
void free_tcp_sock(struct tcp_sock *tsk)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	tsk->ref_cnt -= 1;
	if (tsk->ref_cnt == 0) {
		log(DEBUG, "Free " IP_FMT ":%hu --> " IP_FMT ":%hu.", 
				HOST_IP_FMT_STR(tsk->sk_sip), tsk->sk_sport,
				HOST_IP_FMT_STR(tsk->sk_dip), tsk->sk_dport);
		
		free_ring_buffer(tsk->rcv_buf);
		free_wait_struct(tsk->wait_connect);
		free_wait_struct(tsk->wait_accept);
		free_wait_struct(tsk->wait_recv);
		free_wait_struct(tsk->wait_send);
		free(tsk);
	} 
}

// lookup tcp sock in established_table with key (saddr, daddr, sport, dport)
struct tcp_sock *tcp_sock_lookup_established(u32 saddr, u32 daddr, u16 sport, u16 dport)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	int value = tcp_hash_function(saddr, daddr, sport, dport);
	struct list_head *list = &tcp_established_sock_table[value];

	struct tcp_sock *sock_p;
	list_for_each_entry(sock_p, list, hash_list) {
		if (saddr == sock_p->sk_sip && daddr == sock_p->sk_dip &&
			sport == sock_p->sk_sport && dport == sock_p->sk_dport)
			return sock_p;
	}
	return NULL;
}

// lookup tcp sock in listen_table with key (sport)
//
// In accordance with BSD socket, saddr is in the argument list, but never used.
struct tcp_sock *tcp_sock_lookup_listen(u32 saddr, u16 sport)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	int value = tcp_hash_function(0, 0, sport, 0);
	struct list_head *list = &tcp_listen_sock_table[value];
	struct tcp_sock *sock_p;
	list_for_each_entry(sock_p, list, hash_list) {
		if (sport == sock_p->sk_sport)
			return sock_p;
	}
	return NULL;
}

// lookup tcp sock in both established_table and listen_table
struct tcp_sock *tcp_sock_lookup(struct tcp_cb *cb)
{
	u32 saddr = cb->daddr,
		daddr = cb->saddr;
	u16 sport = cb->dport,
		dport = cb->sport;

	struct tcp_sock *tsk = tcp_sock_lookup_established(saddr, daddr, sport, dport);
	if (!tsk)
		tsk = tcp_sock_lookup_listen(saddr, sport);
	return tsk;
}

// hash tcp sock into bind_table, using sport as the key
static int tcp_bind_hash(struct tcp_sock *tsk)
{
	int bind_hash_value = tcp_hash_function(0, 0, tsk->sk_sport, 0);
	struct list_head *list = &tcp_bind_sock_table[bind_hash_value];
	list_add_head(&tsk->bind_hash_list, list);

	tsk->ref_cnt += 1;
	return 0;
}

// unhash the tcp sock from bind_table
void tcp_bind_unhash(struct tcp_sock *tsk)
{
	if (!list_empty(&tsk->bind_hash_list)) {
		list_delete_entry(&tsk->bind_hash_list);
		free_tcp_sock(tsk);
	}
}

// lookup bind_table to check whether sport is in use
static int tcp_port_in_use(u16 sport)
{
	int value = tcp_hash_function(0, 0, sport, 0);
	struct list_head *list = &tcp_bind_sock_table[value];
	struct tcp_sock *tsk;
	list_for_each_entry(tsk, list, bind_hash_list) {
		if (tsk->sk_sport == sport)
			return 1;
	}
	return 0;
}

// find a free port by looking up bind_table
static u16 tcp_get_port()
{
	for (u16 port = PORT_MIN; port < PORT_MAX; port++) {
		if (!tcp_port_in_use(port))
			return port;
	}
	return 0;
}

// tcp sock tries to use port as its source port
static int tcp_sock_set_sport(struct tcp_sock *tsk, u16 port)
{
	if ((port && tcp_port_in_use(port)) ||
			(!port && !(port = tcp_get_port())))
		return -1;

	tsk->sk_sport = port;
	tcp_bind_hash(tsk);
	return 0;
}

// hash tcp sock into either established_table or listen_table according to its
// TCP_STATE
int tcp_hash(struct tcp_sock *tsk)
{
	struct list_head *list;
	int hash;

	if (tsk->state == TCP_CLOSED)
		return -1;

	if (tsk->state == TCP_LISTEN) {
		hash = tcp_hash_function(0, 0, tsk->sk_sport, 0);
		list = &tcp_listen_sock_table[hash];
	}
	else {
		int hash = tcp_hash_function(tsk->sk_sip, tsk->sk_dip, \
				tsk->sk_sport, tsk->sk_dport); 
		list = &tcp_established_sock_table[hash];

		struct tcp_sock *tmp;
		list_for_each_entry(tmp, list, hash_list) {
			if (tsk->sk_sip == tmp->sk_sip &&
					tsk->sk_dip == tmp->sk_dip &&
					tsk->sk_sport == tmp->sk_sport &&
					tsk->sk_dport == tmp->sk_dport)
				return -1;
		}
	}

	list_add_head(&tsk->hash_list, list);
	tsk->ref_cnt += 1;

	return 0;
}

// unhash tcp sock from established_table or listen_table
void tcp_unhash(struct tcp_sock *tsk)
{
	if (!list_empty(&tsk->hash_list)) {
		list_delete_entry(&tsk->hash_list);
		free_tcp_sock(tsk);
	}
}

// XXX: skaddr here contains network-order variables
int tcp_sock_bind(struct tcp_sock *tsk, struct sock_addr *skaddr)
{
	int err = 0;

	// omit the ip address, and only bind the port
	err = tcp_sock_set_sport(tsk, ntohs(skaddr->port));

	return err;
}

// connect to the remote tcp sock specified by skaddr
//
// XXX: skaddr here contains network-order variables
// 1. initialize the four key tuple (sip, sport, dip, dport);
// 2. hash the tcp sock into bind_table;
// 3. send SYN packet, switch to TCP_SYN_SENT state, wait for the incoming
//    SYN packet by sleep on wait_connect;
// 4. if the SYN packet of the peer arrives, this function is notified, which
//    means the connection is established.
int tcp_sock_connect(struct tcp_sock *tsk, struct sock_addr *skaddr)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	int err = 0;

	tsk->sk_dip = ntohl(skaddr->ip);
	tsk->sk_dport = ntohs(skaddr->port);

	rt_entry_t * rt = longest_prefix_match(tsk->sk_dip);
	if (!rt) {
		log(ERROR, "cannot find route to dest_ip.");
		return -1;
	}
	tsk->sk_sip = rt->iface->ip;

	err = tcp_sock_set_sport(tsk, 0);
	if (err) {
		log(ERROR, "setting sport when connecting failed.");
		return err;
	}

	tcp_set_state(tsk, TCP_SYN_SENT);
	err = tcp_hash(tsk);
	if (err) {
		log(ERROR, "hashing into hash_table when connecting failed.");
		return err;
	}

	tcp_send_control_packet(tsk, TCP_SYN);

	err = sleep_on(tsk->wait_connect);
	if (err) {
		log(ERROR, "sleep wait connect failed.");
		return err;
	}

	return 0;
}

// set backlog (the maximum number of pending connection requst), switch the
// TCP_STATE, and hash the tcp sock into listen_table
int tcp_sock_listen(struct tcp_sock *tsk, int backlog)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	log(DEBUG, "listening port %hu.", tsk->sk_sport);

	tsk->backlog = backlog;
	tcp_set_state(tsk, TCP_LISTEN);
	int err = tcp_hash(tsk);
	if (err) {
		log(ERROR, "hashing into hash_table when listening failed.");
		return err;
	}
	return 0;
}

// check whether the accept queue is full
inline int tcp_sock_accept_queue_full(struct tcp_sock *tsk)
{
	if (tsk->accept_backlog >= tsk->backlog) {
		log(ERROR, "tcp accept queue (%d) is full.", tsk->accept_backlog);
		return 1;
	}
	return 0;
}

// push the tcp sock into accept_queue
inline void tcp_sock_accept_enqueue(struct tcp_sock *tsk)
{
	if (!list_empty(&tsk->list))
		list_delete_entry(&tsk->list);
	list_add_tail(&tsk->list, &tsk->parent->accept_queue);
	tsk->parent->accept_backlog += 1;
}

// pop the first tcp sock of the accept_queue
inline struct tcp_sock *tcp_sock_accept_dequeue(struct tcp_sock *tsk)
{
	struct tcp_sock *new_tsk = list_entry(tsk->accept_queue.next, struct tcp_sock, list);
	list_delete_entry(&new_tsk->list);
	init_list_head(&new_tsk->list);
	tsk->accept_backlog -= 1;

	return new_tsk;
}

// if accept_queue is not emtpy, pop the first tcp sock and accept it,
// otherwise, sleep on the wait_accept for the incoming connection requests
struct tcp_sock *tcp_sock_accept(struct tcp_sock *tsk)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	while (list_empty(&tsk->accept_queue))
		sleep_on(tsk->wait_accept);
	return tcp_sock_accept_dequeue(tsk);
}

// close the tcp sock, by releasing the resources, sending FIN/RST packet
// to the peer, switching TCP_STATE to closed
void tcp_sock_close(struct tcp_sock *tsk)
{
	//fprintf(stdout, "TODO: implement %s please.\n", __FUNCTION__);
	log(DEBUG, "close sock " IP_FMT ":%hu <-> " IP_FMT ":%hu, state %s", 
			HOST_IP_FMT_STR(tsk->sk_sip), tsk->sk_sport,
			HOST_IP_FMT_STR(tsk->sk_dip), tsk->sk_dport, tcp_state_str[tsk->state]);

	switch(tsk->state){
		case TCP_SYN_RECV:
			tcp_set_state(tsk, TCP_FIN_WAIT_1);
			tcp_send_control_packet(tsk, TCP_FIN | TCP_ACK);
			free_tcp_sock(tsk);
			break;

		case TCP_ESTABLISHED:
			tcp_set_state(tsk, TCP_FIN_WAIT_1);
			tcp_send_control_packet(tsk, TCP_FIN | TCP_ACK);
			free_tcp_sock(tsk);
			break;
		
		case TCP_CLOSE_WAIT:
			tcp_set_state(tsk, TCP_LAST_ACK);
			tcp_send_control_packet(tsk, TCP_FIN | TCP_ACK);
			break;
		
		default:
			log(DEBUG, "TCP state default when closing tcp socket");
			tcp_set_state(tsk, TCP_CLOSED);

			tcp_unhash(tsk);
			tcp_bind_unhash(tsk);
			free_tcp_sock(tsk);
			break;
	}
}

int tcp_sock_read(struct tcp_sock *tsk, char *buf, int len) {
	while (ring_buffer_empty(tsk->rcv_buf)) {
		if (tsk->state == TCP_CLOSE_WAIT)
			return 0;
		sleep_on(tsk->wait_recv);
	}

	pthread_mutex_lock(&tsk->rcv_buf_lock);
	int rlen = read_ring_buffer(tsk->rcv_buf, buf, len);
	tsk->rcv_wnd += rlen;
	pthread_mutex_unlock(&tsk->rcv_buf_lock);

	wake_up(tsk->wait_recv);
	return rlen;
}

int tcp_sock_write(struct tcp_sock *tsk, char *buf, int len) {
	int send_len, packet_len;
	int remain_len = len;
	int handled_len = 0;

	while (!list_empty(&tsk->send_buf))
		sleep_on(tsk->wait_send);

	while (remain_len) {
		send_len = min(remain_len, 1514 - ETHER_HDR_SIZE - IP_BASE_HDR_SIZE - TCP_BASE_HDR_SIZE);
		if (tsk->snd_wnd < send_len)
			sleep_on(tsk->wait_send);
		packet_len = send_len + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE;
		char *packet = (char *)malloc(packet_len);
		memcpy(packet + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + TCP_BASE_HDR_SIZE, buf + handled_len, send_len);
		tcp_send_packet(tsk, packet, packet_len);

		tsk->snd_wnd -= send_len;
		remain_len -= send_len;
		handled_len += send_len;
	}

	return handled_len;
}

// Add a packet to the TCP send buffer
void tcp_send_buffer_add_packet(struct tcp_sock *tsk, char *packet, int len) {
    send_buffer_entry_t *send_buffer_entry = (send_buffer_entry_t *)malloc(sizeof(send_buffer_entry_t));
    memset(send_buffer_entry, 0, sizeof(send_buffer_entry_t));

    send_buffer_entry->packet = (char *)malloc(len);
    send_buffer_entry->len = len;
    memcpy(send_buffer_entry->packet, packet, len);

    init_list_head(&send_buffer_entry->list);

	pthread_mutex_lock(&tsk->send_buf_lock);
    list_add_tail(&send_buffer_entry->list, &tsk->send_buf);
	pthread_mutex_unlock(&tsk->send_buf_lock);
}

//Update the TCP send buffer based on the acknowledgment number
void tcp_update_send_buffer(struct tcp_sock *tsk, u32 ack) {
    send_buffer_entry_t *send_buffer_entry, *send_buffer_entry_q;
	pthread_mutex_lock(&tsk->send_buf_lock);

    list_for_each_entry_safe(send_buffer_entry, send_buffer_entry_q, &tsk->send_buf, list) {
        struct tcphdr *tcp = packet_to_tcp_hdr(send_buffer_entry->packet);
        u32 seq = ntohl(tcp->seq);
        // If the sequence number is less than the acknowledgment number, delete the entry
        if (less_than_32b(seq, ack)) {
            list_delete_entry(&send_buffer_entry->list);
            free(send_buffer_entry->packet);
            free(send_buffer_entry);
        }
    }
	pthread_mutex_unlock(&tsk->send_buf_lock);
}

// Retransmit the first packet in the TCP send buffer when ack time exceed
int tcp_retrans_send_buffer(struct tcp_sock *tsk) {
	pthread_mutex_lock(&tsk->send_buf_lock);

    if (list_empty(&tsk->send_buf)) {
        log(ERROR, "no packet to retrans\n");
		pthread_mutex_unlock(&tsk->send_buf_lock);
        return 0;
    }

    // Retrieve the first send buffer entry
    send_buffer_entry_t *first_send_buffer_entry = list_entry(tsk->send_buf.next, send_buffer_entry_t, list);

    char *packet = (char *)malloc(first_send_buffer_entry->len);
    // Copy the packet data and update TCP sequence and acknowledgment numbers
    memcpy(packet, first_send_buffer_entry->packet, first_send_buffer_entry->len);
    struct iphdr *ip = packet_to_ip_hdr(packet);
    struct tcphdr *tcp = packet_to_tcp_hdr(packet);

    tcp->ack = htonl(tsk->rcv_nxt);
    tcp->checksum = tcp_checksum(ip, tcp);
    ip->checksum = ip_checksum(ip);

    // Calculate TCP data length and update TCP send window
    int tcp_data_len = ntohs(ip->tot_len) - IP_BASE_HDR_SIZE - TCP_BASE_HDR_SIZE;
    tsk->snd_wnd -= tcp_data_len;

    log(DEBUG, "retrans seq: %u\n", ntohl(tcp->seq));
	pthread_mutex_unlock(&tsk->send_buf_lock);

    ip_send_packet(packet, first_send_buffer_entry->len);
	return 1;
}

// Add an packet to the TCP receive buffer
int tcp_recv_ofo_buffer_add_packet(struct tcp_sock *tsk, struct tcp_cb *cb) {
	if (cb->pl_len <= 0) {
		return 0;
	}

    recv_ofo_buf_entry_t *recv_ofo_entry = (recv_ofo_buf_entry_t *)malloc(sizeof(recv_ofo_buf_entry_t));
    recv_ofo_entry->seq = cb->seq;
	recv_ofo_entry->seq_end = cb->seq_end;
    recv_ofo_entry->len = cb->pl_len;
    recv_ofo_entry->data = (char *)malloc(cb->pl_len);
    memcpy(recv_ofo_entry->data, cb->payload, cb->pl_len);

    init_list_head(&recv_ofo_entry->list);

    // insert the new entry at the correct position
    recv_ofo_buf_entry_t *entry, *entry_q;
    list_for_each_entry_safe (entry, entry_q, &tsk->rcv_ofo_buf, list) {
        if (recv_ofo_entry->seq == entry->seq)
            return 1; // same seq, do not add
        if (less_than_32b(recv_ofo_entry->seq, entry->seq)) {
            list_add_tail(&recv_ofo_entry->list, &entry->list);
            return 1;
        }
    }
    list_add_tail(&recv_ofo_entry->list, &tsk->rcv_ofo_buf);
	return 1;
}

// Move packets from TCP receive buffer to ring buffer
int tcp_move_recv_ofo_buffer(struct tcp_sock *tsk) {
    recv_ofo_buf_entry_t *entry, *entry_q;

    list_for_each_entry_safe(entry, entry_q, &tsk->rcv_ofo_buf, list) {
        if (tsk->rcv_nxt == entry->seq) {
            // Wait until there is enough space in the receive buffer
            while (ring_buffer_free(tsk->rcv_buf) < entry->len)
                sleep_on(tsk->wait_recv);

            pthread_mutex_lock(&tsk->rcv_buf_lock);
            write_ring_buffer(tsk->rcv_buf, entry->data, entry->len);
            tsk->rcv_wnd -= entry->len;
            pthread_mutex_unlock(&tsk->rcv_buf_lock);
            wake_up(tsk->wait_recv);

            // Update seq and free memory
            tsk->rcv_nxt = entry->seq_end;
            list_delete_entry(&entry->list);
            free(entry->data);
            free(entry);
        } 
		else if (less_than_32b(tsk->rcv_nxt, entry->seq))
            continue; //the next expected sequence number is not reached yet
		else {
            log(ERROR, "rcv_nxt is more than seq, rcv_nxt: %d, seq: %d\n", tsk->rcv_nxt, entry->seq);
            return 0;
        }
    }
    return 1;
}
