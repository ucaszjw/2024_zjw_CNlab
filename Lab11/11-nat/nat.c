#include "nat.h"
#include "ip.h"
#include "icmp.h"
#include "tcp.h"
#include "rtable.h"
#include "log.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

static struct nat_table nat;

// get the interface from iface name
static iface_info_t *if_name_to_iface(const char *if_name)
{
	iface_info_t *iface = NULL;
	list_for_each_entry(iface, &instance->iface_list, list) {
		if (strcmp(iface->name, if_name) == 0)
			return iface;
	}

	log(ERROR, "Could not find the desired interface according to if_name '%s'", if_name);
	return NULL;
}

// determine the direction of the packet, DIR_IN / DIR_OUT / DIR_INVALID
static int get_packet_direction(char *packet)
{
	// fprintf(stdout, "TODO: determine the direction of this packet.\n");
	struct iphdr *iphdr = packet_to_ip_hdr(packet);
	rt_entry_t *src = longest_prefix_match(iphdr->saddr);
	rt_entry_t *dst = longest_prefix_match(iphdr->daddr);

	if (src->iface == nat.internal_iface && dst->iface == nat.external_iface)
		return DIR_OUT;
	else if (src->iface == nat.external_iface && iphdr->daddr == nat.external_iface->ip)
		return DIR_IN;
	else
		return DIR_INVALID;
}

// do translation for the packet: replace the ip/port, recalculate ip & tcp
// checksum, update the statistics of the tcp connection
void do_translation(iface_info_t *iface, char *packet, int len, int dir)
{
	// fprintf(stdout, "TODO: do translation for this packet.\n");
	struct iphdr *iphdr = packet_to_ip_hdr(packet);
	struct tcphdr *tcphdr = packet_to_tcp_hdr(packet);
	u32 saddr = ntohl(iphdr->saddr);
	u32 daddr = ntohl(iphdr->daddr);
	u16 sport = ntohs(tcphdr->sport);
	u16 dport = ntohs(tcphdr->dport);
	u32 raddr = (dir == DIR_OUT) ? daddr : saddr;
	u16 rport = (dir == DIR_OUT) ? dport : sport;

	char *str = (char *)malloc(6);
	memcpy(str, &raddr, 4);
	memcpy(str + 4, &rport, 2);
	u8 hash = hash8(str, 6);
	free(str);
	struct list_head *head = &nat.nat_mapping_list[hash];
	struct nat_mapping *entry = NULL;

	pthread_mutex_lock(&nat.lock);
	list_for_each_entry(entry, head, list) {
		if (raddr != entry->remote_ip || rport != entry->remote_port)
			continue;
		
		int clear = (tcphdr->flags & TCP_RST) != 0;

		if (dir == DIR_IN) {
			if (daddr != entry->external_ip || dport != entry->external_port)
				continue;

			iphdr->daddr = htonl(entry->internal_ip);
			tcphdr->dport = htons(entry->internal_port);
			entry->conn.internal_fin = (tcphdr->flags & TCP_FIN) != 0;
			entry->conn.external_seq_end = tcp_seq_end(iphdr, tcphdr);
			if (tcphdr->flags & TCP_ACK)
				entry->conn.external_ack = tcphdr->ack;
		}
		else {
			if (saddr != entry->internal_ip || sport != entry->internal_port)
				continue;

			iphdr->saddr = htonl(entry->external_ip);
			tcphdr->sport = htons(entry->external_port);
			entry->conn.external_fin = (tcphdr->flags & TCP_FIN) != 0;
			entry->conn.internal_seq_end = tcp_seq_end(iphdr, tcphdr);
			if (tcphdr->flags & TCP_ACK)
				entry->conn.internal_ack = tcphdr->ack;
		}
		pthread_mutex_unlock(&nat.lock);

		iphdr->checksum = ip_checksum(iphdr);
		tcphdr->checksum = tcp_checksum(iphdr, tcphdr);
		entry->update_time = time(NULL);
		ip_send_packet(packet, len);

		if (clear) {
			nat.assigned_ports[entry->external_port] = 0;
			list_delete_entry(&entry->list);
			free(entry);
		}

		return ;
	}

	if (dir == DIR_IN) {
		struct dnat_rule *rule = NULL;
		list_for_each_entry(rule, &nat.rules, list) {
			if (daddr == rule->external_ip && dport == rule->external_port) {
				struct nat_mapping *new = (struct nat_mapping *)malloc(sizeof(struct nat_mapping));
				list_add_tail(&new->list, head);

				new->remote_ip = raddr;
				new->remote_port = rport;
				new->internal_ip = rule->internal_ip;
				new->internal_port = rule->internal_port;
				new->external_ip = daddr;
				new->external_port = dport;
				new->conn.external_fin = (tcphdr->flags & TCP_FIN) != 0;
				new->conn.external_seq_end = tcp_seq_end(iphdr, tcphdr);
				if (tcphdr->flags & TCP_ACK)
					new->conn.external_ack = tcphdr->ack;
				new->update_time = time(NULL);
				pthread_mutex_unlock(&nat.lock);

				iphdr->daddr = htonl(new->internal_ip);
				tcphdr->dport = htons(new->internal_port);
				iphdr->checksum = ip_checksum(iphdr);
				tcphdr->checksum = tcp_checksum(iphdr, tcphdr);
				ip_send_packet(packet, len);
				return ;
			}
		}
	}
	else {
		u16 port;
		for (port = NAT_PORT_MIN; port <= NAT_PORT_MAX; port++) {
			if (!nat.assigned_ports[port]) {
				struct nat_mapping *new = (struct nat_mapping *)malloc(sizeof(struct nat_mapping));
				list_add_tail(&new->list, head);

				new->remote_ip = raddr;
				new->remote_port = rport;
				new->internal_ip = saddr;
				new->internal_port = sport;
				new->external_ip = nat.external_iface->ip;
				new->external_port = port;
				new->conn.internal_fin = (tcphdr->flags & TCP_FIN) != 0;
				new->conn.internal_seq_end = tcp_seq_end(iphdr, tcphdr);
				if (tcphdr->flags & TCP_ACK)
					new->conn.internal_ack = tcphdr->ack;
				new->update_time = time(NULL);
				pthread_mutex_unlock(&nat.lock);

				iphdr->saddr = htonl(new->external_ip);
				tcphdr->sport = htons(new->external_port);
				iphdr->checksum = ip_checksum(iphdr);
				tcphdr->checksum = tcp_checksum(iphdr, tcphdr);
				ip_send_packet(packet, len);
				return ;
			}
		}
	}

	icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
	pthread_mutex_unlock(&nat.lock);
	free(packet);	
}

void nat_translate_packet(iface_info_t *iface, char *packet, int len)
{
	int dir = get_packet_direction(packet);
	if (dir == DIR_INVALID) {
		log(ERROR, "invalid packet direction, drop it.");
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_HOST_UNREACH);
		free(packet);
		return ;
	}

	struct iphdr *ip = packet_to_ip_hdr(packet);
	if (ip->protocol != IPPROTO_TCP) {
		log(ERROR, "received non-TCP packet (0x%0hhx), drop it", ip->protocol);
		free(packet);
		return ;
	}

	do_translation(iface, packet, len, dir);
}

// check whether the flow is finished according to FIN bit and sequence number
// XXX: seq_end is calculated by `tcp_seq_end` in tcp.h
static int is_flow_finished(struct nat_connection *conn)
{
    return (conn->internal_fin && conn->external_fin) && \
            (conn->internal_ack >= conn->external_seq_end) && \
            (conn->external_ack >= conn->internal_seq_end);
}

// nat timeout thread: find the finished flows, remove them and free port
// resource
void *nat_timeout()
{
	while (1) {
		// fprintf(stdout, "TODO: sweep finished flows periodically.\n");
		sleep(1);
		pthread_mutex_lock(&nat.lock);
		for (int i = 0; i < HASH_8BITS; i++) {
			struct nat_mapping *entry = NULL, *map_q = NULL;
			list_for_each_entry_safe(entry, map_q, &nat.nat_mapping_list[i], list) {
				if (time(NULL) - entry->update_time > TCP_ESTABLISHED_TIMEOUT || is_flow_finished(&entry->conn)) {
					nat.assigned_ports[entry->external_port] = 0;
					list_delete_entry(&entry->list);
					free(entry);
				}
			}
		}
		pthread_mutex_unlock(&nat.lock);
	}

	return NULL;
}

int parse_config(const char *filename)
{
	// fprintf(stdout, "TODO: parse config file, including i-iface, e-iface (and dnat-rules if existing).\n");
	FILE *file = fopen(filename, "r");
	if (!file) {
		log(ERROR, "Could not open the config file '%s'", filename);
		return -1;
	}

	char *line = (char *)malloc(128);
	while (fgets(line, 128, file)) {
		if (line[0] == '\n' || line[0] == '#')
			continue;
		else if (line[0] == 'i') {
			char *if_name = line + 16;
			nat.internal_iface = if_name_to_iface(if_name);
			continue;
		}
		else if (line[0] == 'e') {
			char *if_name = line + 16;
			nat.external_iface = if_name_to_iface(if_name);
			continue;
		}
		else if (line[0] == 'd') {
			struct dnat_rule *rule = (struct dnat_rule *)malloc(sizeof(struct dnat_rule));
			char *drule = line + 12;
			u8 external_ip[4], internal_ip[4];
			sscanf(drule, "%hhu.%hhu.%hhu.%hhu:%hu -> %hhu.%hhu.%hhu.%hhu:%hu", 
				&external_ip[0], &external_ip[1], &external_ip[2], &external_ip[3], &rule->external_port,
				&internal_ip[0], &internal_ip[1], &internal_ip[2], &internal_ip[3], &rule->internal_port);

			rule->external_ip = (external_ip[0] << 24) | (external_ip[1] << 16) | (external_ip[2] << 8) | external_ip[3];
			rule->internal_ip = (internal_ip[0] << 24) | (internal_ip[1] << 16) | (internal_ip[2] << 8) | internal_ip[3];

			init_list_head(&rule->list);
			list_add_tail(&rule->list, &nat.rules);
			nat.assigned_ports[rule->external_port] = 1;
			continue;
		}
	}
	fclose(file);
	free(line);
	return 0;
}

// initialize
void nat_init(const char *config_file)
{
	memset(&nat, 0, sizeof(nat));

	for (int i = 0; i < HASH_8BITS; i++)
		init_list_head(&nat.nat_mapping_list[i]);

	init_list_head(&nat.rules);

	// seems unnecessary
	memset(nat.assigned_ports, 0, sizeof(nat.assigned_ports));

	parse_config(config_file);

	pthread_mutex_init(&nat.lock, NULL);

	pthread_create(&nat.thread, NULL, nat_timeout, NULL);
}

void nat_exit()
{
	// fprintf(stdout, "TODO: release all resources allocated.\n");
	for (int i = 0; i < HASH_8BITS; i++) {
		struct nat_mapping *entry = NULL, *map_q = NULL;
		list_for_each_entry_safe(entry, map_q, &nat.nat_mapping_list[i], list) {
			list_delete_entry(&entry->list);
			free(entry);
		}

		struct dnat_rule *rule = NULL, *rule_q = NULL;
		list_for_each_entry_safe(rule, rule_q, &nat.rules, list) {
			list_delete_entry(&rule->list);
			free(rule);
		}
	}
	
}
