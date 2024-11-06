#include "mospf_daemon.h"
#include "mospf_proto.h"
#include "mospf_nbr.h"
#include "mospf_database.h"
#include "rtable.h"

#include "ip.h"
#include "arp.h"

#include "list.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

extern ustack_t *instance;

pthread_mutex_t mospf_lock;

int node_num;
u32 node_map[MAX_NODE_NUM];
int graph[MAX_NODE_NUM][MAX_NODE_NUM];
int prev[MAX_NODE_NUM];
int stack[MAX_NODE_NUM];
int stack_top;

void *sending_mospf_hello_thread(void *param);
void *sending_mospf_lsu_thread(void *param);
void *checking_nbr_thread(void *param);
void *checking_database_thread(void *param);
void send_mospf_lsu_packet(void);
void update_rtable(void);
void print_database(void);

void mospf_init()
{
	pthread_mutex_init(&mospf_lock, NULL);

	instance->area_id = 0;
	// get the ip address of the first interface
	iface_info_t *iface = list_entry(instance->iface_list.next, iface_info_t, list);
	instance->router_id = iface->ip;
	instance->sequence_num = 0;
	instance->lsuint = MOSPF_DEFAULT_LSUINT;

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		iface->helloint = MOSPF_DEFAULT_HELLOINT;
		init_list_head(&iface->nbr_list);
	}

	init_mospf_db();
}


void mospf_run()
{
	pthread_t hello, lsu, nbr, db;
	pthread_create(&hello, NULL, sending_mospf_hello_thread, NULL);
	pthread_create(&lsu, NULL, sending_mospf_lsu_thread, NULL);
	pthread_create(&nbr, NULL, checking_nbr_thread, NULL);
	pthread_create(&db, NULL, checking_database_thread, NULL);
}

void *sending_mospf_hello_thread(void *param)
{
	// fprintf(stdout, "TODO: send mOSPF Hello message periodically.\n");
	while (1)
	{
		sleep(MOSPF_DEFAULT_HELLOINT);
		pthread_mutex_lock(&mospf_lock);

		iface_info_t *iface = NULL;
		list_for_each_entry(iface, &instance->iface_list, list) {
			int mospf_len = MOSPF_HDR_SIZE + MOSPF_HELLO_SIZE;
			int packet_len = ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + mospf_len;
			char *packet = (char *)malloc(packet_len);
			struct ether_header *ethdr = (struct ether_header *)packet;
			struct iphdr *iphdr = packet_to_ip_hdr(packet);
			struct mospf_hdr *mospfhdr = (struct mospf_hdr *)((char *)iphdr + IP_BASE_HDR_SIZE);
			struct mospf_hello *hello = (struct mospf_hello *)((char *)mospfhdr + MOSPF_HDR_SIZE);

			memset(packet, 0, packet_len);

			mospf_init_hello(hello, iface->mask);
			mospf_init_hdr(mospfhdr, MOSPF_TYPE_HELLO, mospf_len, instance->router_id, instance->area_id);
			mospfhdr->checksum = mospf_checksum(mospfhdr);

			ip_init_hdr(iphdr, iface->ip, MOSPF_ALLSPFRouters, packet_len - ETHER_HDR_SIZE, IPPROTO_MOSPF);

			memcpy(ethdr->ether_dhost, "\x01\x00\x5e\x00\x00\x05", ETH_ALEN);
			memcpy(ethdr->ether_shost, iface->mac, ETH_ALEN);
			ethdr->ether_type = htons(ETH_P_IP);

			iface_send_packet(iface, packet, packet_len);
		}

		pthread_mutex_unlock(&mospf_lock);
	}
	
	return NULL;
}

void *checking_nbr_thread(void *param)
{
	// fprintf(stdout, "TODO: neighbor list timeout operation.\n");
	while (1)
	{
		sleep(1);
		pthread_mutex_lock(&mospf_lock);

		int delete = 0;
		iface_info_t *iface = NULL;
		list_for_each_entry(iface, &instance->iface_list, list) {
			mospf_nbr_t *nbr = NULL;
			mospf_nbr_t *nbr_q = NULL;
			list_for_each_entry_safe(nbr, nbr_q, &iface->nbr_list, list) {
				if (nbr->alive > 3 * iface->helloint){
					delete = 1;
					iface->num_nbr--;
					list_delete_entry(&nbr->list);
					free(nbr);
				}
				else
					nbr->alive++;
			}
		}

		if (delete)
			send_mospf_lsu_packet();
		
		pthread_mutex_unlock(&mospf_lock);
	}
	
	return NULL;
}

void *checking_database_thread(void *param)
{
	// fprintf(stdout, "TODO: link state database timeout operation.\n");
	while (1)
	{
		sleep(1);
		pthread_mutex_lock(&mospf_lock);

		int delete = 0;
		mospf_db_entry_t *db = NULL;
		mospf_db_entry_t *db_q = NULL;
		list_for_each_entry_safe(db, db_q, &mospf_db, list) {
			if (db->alive > MOSPF_DATABASE_TIMEOUT){
				delete = 1;
				list_delete_entry(&db->list);
				free(db->array);
				free(db);
			}
			else
				db->alive++;
		}

		if (delete)
			update_rtable();

		pthread_mutex_unlock(&mospf_lock);
	}
	
	return NULL;
}

void handle_mospf_hello(iface_info_t *iface, const char *packet, int len)
{
	// fprintf(stdout, "TODO: handle mOSPF Hello message.\n");
	struct iphdr *iphdr = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospfhdr = (struct mospf_hdr *)((char *)iphdr + IP_HDR_SIZE(iphdr));
	struct mospf_hello *hello = (struct mospf_hello *)((char *)mospfhdr + MOSPF_HDR_SIZE);
	pthread_mutex_lock(&mospf_lock);

	mospf_nbr_t *nbr = NULL;
	list_for_each_entry(nbr, &iface->nbr_list, list) {
		if (nbr->nbr_id == ntohl(mospfhdr->rid)) {
			nbr->alive = 0;
			nbr->nbr_ip = ntohl(iphdr->saddr);
			nbr->nbr_mask = ntohl(hello->mask);

			pthread_mutex_unlock(&mospf_lock);
			return;
		}
	}

	nbr = (mospf_nbr_t *)malloc(sizeof(mospf_nbr_t));
	nbr->nbr_id = ntohl(mospfhdr->rid);
	nbr->nbr_ip = ntohl(iphdr->saddr);
	nbr->nbr_mask = ntohl(hello->mask);
	nbr->alive = 0;
	init_list_head(&nbr->list);
	list_add_tail(&nbr->list, &iface->nbr_list);
	iface->num_nbr++;

	send_mospf_lsu_packet();
	pthread_mutex_unlock(&mospf_lock);
}

void *sending_mospf_lsu_thread(void *param)
{
	// fprintf(stdout, "TODO: send mOSPF LSU message periodically.\n");
	while (1)
	{
		sleep(MOSPF_DEFAULT_LSUINT);
		pthread_mutex_lock(&mospf_lock);
		send_mospf_lsu_packet();
		print_database();
		pthread_mutex_unlock(&mospf_lock);
	}
	return NULL;
}

void send_mospf_lsu_packet()
{
	int lsa_num = 0;
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		lsa_num += iface->num_nbr ? iface->num_nbr : 1;
	}

	int mospf_len = MOSPF_HDR_SIZE + MOSPF_LSU_SIZE + lsa_num * MOSPF_LSA_SIZE;
	char *packet = (char *)malloc(mospf_len);
	memset(packet, 0, mospf_len);
	struct mospf_hdr *mospfhdr = (struct mospf_hdr *)packet;
	struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospfhdr + MOSPF_HDR_SIZE);
	struct mospf_lsa *lsa = (struct mospf_lsa *)((char *)lsu + MOSPF_LSU_SIZE);

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (iface->num_nbr) {
			mospf_nbr_t *nbr = NULL;
			list_for_each_entry(nbr, &iface->nbr_list, list) {
				lsa->network = iface->ip & iface->mask;
				lsa->mask = iface->mask;
				lsa->rid = nbr->nbr_id;
				lsa++;
			}
		}
		else {
			lsa->network = iface->ip & iface->mask;
			lsa->mask = iface->mask;
			lsa->rid = 0;
			lsa++;
		}
	}

	mospf_init_lsu(lsu, lsa_num);
	instance->sequence_num++;

	mospf_init_hdr(mospfhdr, MOSPF_TYPE_LSU, mospf_len, instance->router_id, instance->area_id);
	mospfhdr->checksum = mospf_checksum(mospfhdr);

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (iface->num_nbr) {
			mospf_nbr_t *nbr = NULL;
			list_for_each_entry(nbr, &iface->nbr_list, list) {
				char *packet_send = (char *)malloc(ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + mospf_len);
				struct ether_header *ethdr = (struct ether_header *)packet_send;
				struct iphdr *iphdr = (struct iphdr *)(packet_send + ETHER_HDR_SIZE);
				char *mospf_message = packet_send + ETHER_HDR_SIZE + IP_BASE_HDR_SIZE;

				memset(packet_send, 0, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + mospf_len);
				memcpy(mospf_message, packet, mospf_len);
				ip_init_hdr(iphdr, iface->ip, nbr->nbr_ip, mospf_len + IP_BASE_HDR_SIZE, IPPROTO_MOSPF);
				memcpy(ethdr->ether_shost, iface->mac, ETH_ALEN);
				ethdr->ether_type = htons(ETH_P_IP);

				iface_send_packet_by_arp(iface, nbr->nbr_ip, packet_send, ETHER_HDR_SIZE + IP_BASE_HDR_SIZE + mospf_len);
			}
		}
	}
	free(packet);
}
	

void handle_mospf_lsu(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stdout, "TODO: handle mOSPF LSU message.\n");
	struct ether_header *ethdr = (struct ether_header *)packet;
	struct iphdr *iphdr = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospfhdr = (struct mospf_hdr *)((char *)iphdr + IP_HDR_SIZE(iphdr));
	struct mospf_lsu *lsu = (struct mospf_lsu *)((char *)mospfhdr + MOSPF_HDR_SIZE);
	struct mospf_lsa *lsa = (struct mospf_lsa *)((char *)lsu + MOSPF_LSU_SIZE);

	pthread_mutex_lock(&mospf_lock);
	if (instance->router_id == ntohl(mospfhdr->rid)) {
		pthread_mutex_unlock(&mospf_lock);
		return;
	}

	int update = 0;
	int exist = 0;
	mospf_db_entry_t *db = NULL;
	list_for_each_entry(db, &mospf_db, list) {
		if (db->rid == ntohl(mospfhdr->rid)) {
			exist = 1;
			if (db->seq < ntohs(lsu->seq)) {
				db->seq = ntohs(lsu->seq);
				db->alive = 0;
				db->nadv = ntohl(lsu->nadv);
				for (int i=0; i<db->nadv; i++) {
					db->array[i].network = lsa[i].network;
					db->array[i].mask = lsa[i].mask;
					db->array[i].rid = lsa[i].rid;
				}
				update = 1;
			}
		}
	}

	if (!exist) {
		db = (mospf_db_entry_t *)malloc(sizeof(mospf_db_entry_t));
		db->rid = ntohl(mospfhdr->rid);
		db->seq = ntohs(lsu->seq);
		db->alive = 0;
		db->nadv = ntohl(lsu->nadv);
		db->array = (struct mospf_lsa *)malloc(MOSPF_LSA_SIZE * db->nadv);
		for (int i=0; i<db->nadv; i++) {
			db->array[i].network = lsa[i].network;
			db->array[i].mask = lsa[i].mask;
			db->array[i].rid = lsa[i].rid;
		}
		init_list_head(&db->list);
		list_add_tail(&db->list, &mospf_db);
		update = 1;
	}

	if (!update) {
		pthread_mutex_unlock(&mospf_lock);
		return;
	}

	lsu->ttl--;
	if (lsu->ttl > 0) {
		mospfhdr->checksum = mospf_checksum(mospfhdr);
		iface_info_t *iface_out = NULL;

		list_for_each_entry(iface_out, &instance->iface_list, list) {
			if (iface_out->num_nbr && iface_out != iface) {
				mospf_nbr_t *nbr = NULL;
				list_for_each_entry(nbr, &iface_out->nbr_list, list) {
					char *packet_send = (char *)malloc(len);
					struct ether_header *ethdr_send = (struct ether_header *)packet_send;
					struct iphdr *iphdr_send = (struct iphdr *)(packet_send + ETHER_HDR_SIZE);
					memcpy(packet_send, packet, len);

					iphdr_send->daddr = htonl(nbr->nbr_ip);
					iphdr_send->checksum = ip_checksum(iphdr_send);

					memcpy(ethdr_send->ether_shost, iface_out->mac, ETH_ALEN);
					ethdr_send->ether_type = htons(ETH_P_IP);

					iface_send_packet_by_arp(iface_out, nbr->nbr_ip, packet_send, len);
				}
			}
		}
	}
	update_rtable();
	pthread_mutex_unlock(&mospf_lock);
}

void handle_mospf_packet(iface_info_t *iface, char *packet, int len)
{
	struct iphdr *ip = (struct iphdr *)(packet + ETHER_HDR_SIZE);
	struct mospf_hdr *mospf = (struct mospf_hdr *)((char *)ip + IP_HDR_SIZE(ip));

	if (mospf->version != MOSPF_VERSION) {
		log(ERROR, "received mospf packet with incorrect version (%d)", mospf->version);
		return ;
	}
	if (mospf->checksum != mospf_checksum(mospf)) {
		log(ERROR, "received mospf packet with incorrect checksum");
		return ;
	}
	if (ntohl(mospf->aid) != instance->area_id) {
		log(ERROR, "received mospf packet with incorrect area id");
		return ;
	}

	switch (mospf->type) {
		case MOSPF_TYPE_HELLO:
			handle_mospf_hello(iface, packet, len);
			break;
		case MOSPF_TYPE_LSU:
			handle_mospf_lsu(iface, packet, len);
			break;
		default:
			log(ERROR, "received mospf packet with unknown type (%d).", mospf->type);
			break;
	}
}

int rid_exist(u32 rid)
{
	for (int i=0; i<node_num; i++)
		if (node_map[i] == rid)
			return 1;

	return 0;
}

int get_node_index(u32 rid)
{
	for (int i=0; i<node_num; i++)
		if (node_map[i] == rid)
			return i;

	return -1;
}

void update_rtable(void)
{
	rt_entry_t *rt_entry, *rt_q;
	list_for_each_entry_safe(rt_entry, rt_q, &rtable, list) {
		if (rt_entry->gw) 
			remove_rt_entry(rt_entry);
	}


	node_map[0] = instance->router_id;
	node_num = 1;
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (iface->num_nbr) {
			mospf_nbr_t *nbr = NULL;
			list_for_each_entry(nbr, &iface->nbr_list, list) {
				if (!rid_exist(nbr->nbr_id)) 
					node_map[node_num++] = nbr->nbr_id;
			}
		}
	}

	mospf_db_entry_t *db = NULL;
	list_for_each_entry(db, &mospf_db, list) {
		if (!rid_exist(db->rid))
			node_map[node_num++] = db->rid;
		for (int i=0; i<db->nadv; i++)
			if (db->array[i].rid && !rid_exist(db->array[i].rid))
				node_map[node_num++] = db->array[i].rid;
	}


	for (int i=0; i<node_num; i++) {
		for (int j=0; j<node_num; j++) {
			if (i == j)
				graph[i][j] = 0;
			else
				graph[i][j] = 0x1fffffff; // seen as the maximum value
		}
	}

	iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (iface->num_nbr) {
			mospf_nbr_t *nbr = NULL;
			list_for_each_entry(nbr, &iface->nbr_list, list) {
				graph[0][get_node_index(nbr->nbr_id)] = 1;
				graph[get_node_index(nbr->nbr_id)][0] = 1;
			}
		}
	}

	db = NULL;
	list_for_each_entry(db, &mospf_db, list) {
		int src = get_node_index(db->rid);
		for (int i=0; i<db->nadv; i++) {
			if (db->array[i].rid) {
				int dst = get_node_index(db->array[i].rid);
				graph[src][dst] = 1;
				graph[dst][src] = 1;
			}
		}
	}


	int dist[MAX_NODE_NUM];
	int visited[MAX_NODE_NUM];
	for (int i=0; i<node_num; i++) {
		dist[i] = 0x1fffffff;
		visited[i] = 0;
		prev[i] = -1;
	}
	dist[0] = 0;
	stack_top = 0;

	for (int i=0; i<node_num; i++) {
		int min = 0x1fffffff;
		int u = -1;
		for (int j=0; j<node_num; j++) {
			if (!visited[j] && dist[j] < min) {
				min = dist[j];
				u = j;
			}
		}

		if (u == -1)
			break;

		visited[u] = 1;
		stack[stack_top++] = u;

		for (int v=0; v<node_num; v++) {
			if (!visited[v] && dist[u] + graph[u][v] < dist[v]) {
				dist[v] = dist[u] + graph[u][v];
				prev[v] = u;
			}
		}
	}


	db = NULL;
	iface_info_t *iface_out = NULL;
	int current_node = 0;
	int find = 0;
	u32 gw;
	for (int i = 1; i < stack_top; i++)
	{
		current_node = stack[i];
		mospf_db_entry_t *db_tmp = NULL;
		list_for_each_entry(db_tmp, &mospf_db, list)
		{
			if (db_tmp->rid == node_map[current_node]) {
				db = db_tmp;
				break;
			}
		}

		if (db == NULL)
			continue;

		while (prev[current_node] != 0)
			current_node = prev[current_node];
		
		iface = NULL;
		list_for_each_entry(iface, &instance->iface_list, list)
		{
			if (iface->num_nbr)
			{
				mospf_nbr_t *nbr = NULL;
				list_for_each_entry(nbr, &iface->nbr_list, list)
				{
					if (nbr->nbr_id == node_map[current_node])
					{
						iface_out = iface;
						gw = nbr->nbr_ip;
						break;
					}
				}
			}
		}

		if (iface_out == NULL)
			continue;

		for (int j = 0; j < db->nadv; j++)
		{
			find = 0;
			rt_entry_t *rt_entry = NULL;
			list_for_each_entry(rt_entry, &rtable, list)
			{
				if (rt_entry->dest == db->array[j].network) {
					find = 1;
					break;
				}
			}

			if (!find)
			{
				rt_entry = new_rt_entry(db->array[j].network, db->array[j].mask, gw, iface_out);
				add_rt_entry(rt_entry);
			}
		}
	}


	print_rtable();

	return;
}