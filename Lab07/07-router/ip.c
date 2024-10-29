#include "ip.h"
#include "icmp.h"
#include "types.h"
#include "rtable.h"
#include "arp.h"

#include <stdio.h>
#include <stdlib.h>

// handle ip packet
//
// If the packet is ICMP echo request and the destination IP address is equal to
// the IP address of the iface, send ICMP echo reply; otherwise, forward the
// packet.
void handle_ip_packet(iface_info_t *iface, char *packet, int len)
{
	// fprintf(stderr, "TODO: handle ip packet.\n");
	struct iphdr *iphdr = packet_to_ip_hdr(packet);
	struct icmphdr *icmphdr = (struct icmphdr *)IP_DATA(iphdr);
	u32 dest = ntohl(iphdr->daddr);
	u8 protocol = iphdr->protocol;
	u8 type = icmphdr->type;

	// check if the packet is ICMP echo request and the destination IP address is equal to the IP address of the iface
	if (dest == iface->ip && protocol == IPPROTO_ICMP && type == ICMP_ECHOREQUEST) {
		icmp_send_packet(packet, len, ICMP_ECHOREPLY, 0);
		free(packet);
		return;
	}

	// forward the packet
	iphdr->ttl--;

	// check if the TTL is less than or equal to 0
	if (iphdr->ttl <= 0) {
		icmp_send_packet(packet, len, ICMP_TIME_EXCEEDED, ICMP_EXC_TTL);
		free(packet);
		return;
	}
	// update the checksum of the IP header
	iphdr->checksum = ip_checksum(iphdr);
	// search the routing table for the longest prefix match
	rt_entry_t *match = longest_prefix_match(dest);
	if (match == NULL) {
		icmp_send_packet(packet, len, ICMP_DEST_UNREACH, ICMP_NET_UNREACH);
		free(packet);
		return;
	}

	// check if the destination IP address is in the same network with the iface
	u32 nextip;
	if (match->gw == 0)
		nextip = dest;
	else
		nextip = match->gw;
	iface_send_packet_by_arp(match->iface, nextip, packet, len);
}
