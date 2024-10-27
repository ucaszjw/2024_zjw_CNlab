#include "icmp.h"
#include "ip.h"
#include "rtable.h"
#include "arp.h"
#include "base.h"

#include <stdio.h>
#include <stdlib.h>

// send icmp packet
void icmp_send_packet(const char *in_pkt, int len, u8 type, u8 code)
{
	// fprintf(stderr, "TODO: malloc and send icmp packet.\n");
	struct iphdr *iphdr = packet_to_ip_hdr(in_pkt);
	char *ipdata = IP_DATA(iphdr);

	int icmp_len;
	if (type == ICMP_ECHOREPLY)
		icmp_len = ntohs(iphdr->tot_len) - IP_HDR_SIZE(iphdr);
	else
		icmp_len = ICMP_HDR_SIZE + IP_HDR_SIZE(iphdr) + ICMP_COPIED_DATA_LEN;
	int res_len = IP_BASE_HDR_SIZE + ETHER_HDR_SIZE + icmp_len;

	char *res = (char *)malloc(res_len);
	memset(res, 0, res_len);

	struct iphdr *res_iphdr = packet_to_ip_hdr(res);
	if (type == ICMP_ECHOREPLY)
		ip_init_hdr(res_iphdr, ntohl(iphdr->daddr), ntohl(iphdr->saddr), icmp_len + IP_BASE_HDR_SIZE, IPPROTO_ICMP);
	else{
		rt_entry_t *match = longest_prefix_match(ntohl(iphdr->saddr));
		if (match == NULL) {
			free(res);
			return;
		}
		ip_init_hdr(res_iphdr, match->iface->ip, ntohl(iphdr->saddr), icmp_len + IP_BASE_HDR_SIZE, IPPROTO_ICMP);
	}

	char *res_ipdata = IP_DATA(res_iphdr);
	struct icmphdr *res_icmphdr = (struct icmphdr *)res_ipdata;
	if (type == ICMP_ECHOREPLY)
		memcpy(res_ipdata, ipdata, icmp_len);
	else
		memcpy(res_ipdata + ICMP_HDR_SIZE, iphdr, icmp_len - ICMP_HDR_SIZE);

	res_icmphdr->type = type;
	res_icmphdr->code = code;
	res_icmphdr->checksum = icmp_checksum(res_icmphdr, icmp_len);

	ip_send_packet(res, res_len);
}
