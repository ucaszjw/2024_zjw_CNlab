#include "arpcache.h"
#include "arp.h"
#include "ether.h"
#include "icmp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static arpcache_t arpcache;

void *arpcache_sweep(void *arg);
// initialize IP->mac mapping, request list, lock and sweeping thread
void arpcache_init()
{
	bzero(&arpcache, sizeof(arpcache_t));

	init_list_head(&(arpcache.req_list));

	pthread_mutex_init(&arpcache.lock, NULL);

	pthread_create(&arpcache.thread, NULL, arpcache_sweep, NULL);
}

// release all the resources when exiting
void arpcache_destroy()
{
	pthread_mutex_lock(&arpcache.lock);

	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		struct cached_pkt *pkt_entry = NULL, *pkt_q;
		list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
			list_delete_entry(&(pkt_entry->list));
			free(pkt_entry->packet);
			free(pkt_entry);
		}

		list_delete_entry(&(req_entry->list));
		free(req_entry);
	}

	pthread_kill(arpcache.thread, SIGTERM);

	pthread_mutex_unlock(&arpcache.lock);
}

// lookup the IP->mac mapping
//
// traverse the table to find whether there is an entry with the same IP
// and mac address with the given arguments
int arpcache_lookup(u32 ip4, u8 mac[ETH_ALEN])
{
	// fprintf(stderr, "TODO: lookup ip address in arp cache.\n");
	pthread_mutex_lock(&arpcache.lock);
	for (int i = 0; i < MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].ip4 == ip4 && arpcache.entries[i].valid) {
			memcpy(mac, arpcache.entries[i].mac, ETH_ALEN);
			pthread_mutex_unlock(&arpcache.lock);
			return 1;
		}
	}
	pthread_mutex_unlock(&arpcache.lock);
	return 0;
}

// append the packet to arpcache
//
// Lookup in the list which stores pending packets, if there is already an
// entry with the same IP address and iface (which means the corresponding arp
// request has been sent out), just append this packet at the tail of that entry
// (the entry may contain more than one packet); otherwise, malloc a new entry
// with the given IP address and iface, append the packet, and send arp request.
void arpcache_append_packet(iface_info_t *iface, u32 ip4, char *packet, int len)
{
	// fprintf(stderr, "TODO: append the ip address if lookup failed, and send arp request if necessary.\n");
	pthread_mutex_lock(&arpcache.lock);
	struct arp_req *req_entry = NULL;
	struct arp_req *req_q;

	// find the entry with the same IP address
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		if (req_entry->ip4 == ip4) {
			struct cached_pkt *pkt_entry = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
			init_list_head(&(pkt_entry->list));
			pkt_entry->packet = packet;
			pkt_entry->len = len;
			list_add_tail(&(pkt_entry->list), &(req_entry->cached_packets));
			pthread_mutex_unlock(&arpcache.lock);
			return;
		}
	}

	// no entry found, malloc a new entry
	req_entry = (struct arp_req *)malloc(sizeof(struct arp_req));
	init_list_head(&(req_entry->list));
	init_list_head(&(req_entry->cached_packets));
	req_entry->iface = iface;
	req_entry->ip4 = ip4;
	req_entry->sent = time(NULL);
	req_entry->retries = 0;
	list_add_tail(&(req_entry->list), &(arpcache.req_list));

	struct cached_pkt *pkt_entry = (struct cached_pkt *)malloc(sizeof(struct cached_pkt));
	pkt_entry->packet = packet;
	pkt_entry->len = len;
	list_add_tail(&(pkt_entry->list), &(req_entry->cached_packets));

	pthread_mutex_unlock(&arpcache.lock);
	arp_send_request(iface, ip4);
}

// insert the IP->mac mapping into arpcache, if there are pending packets
// waiting for this mapping, fill the ethernet header for each of them, and send
// them out
void arpcache_insert(u32 ip4, u8 mac[ETH_ALEN])
{
	// fprintf(stderr, "TODO: insert ip->mac entry, and send all the pending packets.\n");
	pthread_mutex_lock(&arpcache.lock);
	int i;

	// update the entry if it already exists
	for (i = 0; i < MAX_ARP_SIZE; i++) {
		if (arpcache.entries[i].valid && arpcache.entries[i].ip4 == ip4){
			arpcache.entries[i].added = time(NULL);
			memcpy(arpcache.entries[i].mac, mac, ETH_ALEN);
			pthread_mutex_unlock(&arpcache.lock);
			return;
		}
	}

	// search for an empty entry
	for (i = 0; i < MAX_ARP_SIZE; i++) 
		if (!arpcache.entries[i].valid) 
			break;
	
	// if no empty entry, replace a random entry
	if (i == MAX_ARP_SIZE)
		i = rand() % MAX_ARP_SIZE;

	arpcache.entries[i].ip4 = ip4;
	arpcache.entries[i].added = time(NULL);
	arpcache.entries[i].valid = 1;
	memcpy(arpcache.entries[i].mac, mac, ETH_ALEN);

	// send all the pending packets with the same IP address of the new entry
	struct arp_req *req_entry = NULL, *req_q;
	list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
		if (req_entry->ip4 == ip4) {
			struct cached_pkt *pkt_entry = NULL, *pkt_q;
			list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
				memcpy(pkt_entry->packet, mac, ETH_ALEN);
				iface_send_packet(req_entry->iface, pkt_entry->packet, pkt_entry->len);
				list_delete_entry(&(pkt_entry->list));
				free(pkt_entry);
			}
			list_delete_entry(&(req_entry->list));
			free(req_entry);
		}
	}
	pthread_mutex_unlock(&arpcache.lock);
}

// sweep arpcache periodically
//
// For the IP->mac entry, if the entry has been in the table for more than 15
// seconds, remove it from the table.
// For the pending packets, if the arp request is sent out 1 second ago, while 
// the reply has not been received, retransmit the arp request. If the arp
// request has been sent 5 times without receiving arp reply, for each
// pending packet, send icmp packet (DEST_HOST_UNREACHABLE), and drop these
// packets.
void *arpcache_sweep(void *arg) 
{
	while (1) {
		sleep(1);
		// fprintf(stderr, "TODO: sweep arpcache periodically: remove old entries, resend arp requests .\n");
		pthread_mutex_lock(&arpcache.lock);
		for (int i = 0; i < MAX_ARP_SIZE; i++) 
			if (arpcache.entries[i].valid && time(NULL) - arpcache.entries[i].added > ARP_ENTRY_TIMEOUT) 
				arpcache.entries[i].valid = 0;

		struct list_head temp;
		init_list_head(&temp);

		struct arp_req *req_entry = NULL, *req_q;
		list_for_each_entry_safe(req_entry, req_q, &(arpcache.req_list), list) {
			if (time(NULL) - req_entry->sent >= 1) {
				req_entry->retries++;
				req_entry->sent = time(NULL);
				if (req_entry->retries > ARP_REQUEST_MAX_RETRIES) {
					struct cached_pkt *pkt_entry = NULL, *pkt_q;
					list_for_each_entry_safe(pkt_entry, pkt_q, &(req_entry->cached_packets), list) {
						list_delete_entry(&(pkt_entry->list));
						list_add_tail(&(pkt_entry->list), &temp);
					}
					list_delete_entry(&(req_entry->list));
					free(req_entry);
				}
				else
					arp_send_request(req_entry->iface, req_entry->ip4);
			}
		}
		pthread_mutex_unlock(&arpcache.lock);

		struct cached_pkt *pkt_entry = NULL, *pkt_q;
		list_for_each_entry_safe(pkt_entry, pkt_q, &temp, list) {
			icmp_send_packet(pkt_entry->packet, pkt_entry->len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
			free(pkt_entry);
		}
	}

	return NULL;
}
