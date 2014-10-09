/*
 * Network.h
 *
 *  Created on: Feb 20, 2014
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef NETWORK_H_
#define NETWORK_H_

#include <cstdint>
#include <netinet/ether.h> // ethernet header structure
#include <netinet/ip.h> // IP header structure
#include <netinet/udp.h> // UDP header structure
#include <net/if_arp.h>// UDP header structure

namespace na62 {

/*
 * UDP/IP complete header
 */
struct UDP_HDR { // 42 byte
	struct ether_header eth; // 14 byte
	struct iphdr ip; //20
	struct udphdr udp; // 8 byte

	/**
	 * Fills the IP and UDP payload length corresponding fields
	 * @param length u_int The length of the UDP-Payload (actual data)
	 */
	void setPayloadSize(unsigned int dataLength) {
		ip.tot_len = htons(
				sizeof(struct iphdr) + sizeof(struct udphdr) + dataLength);
		udp.len = htons(sizeof(struct udphdr) + dataLength);
	}

	inline bool isFragment() {
		/*
		 * frag_off stores offset or "more fragments flag"
		 */
		return ntohs(ip.frag_off) & (IP_OFFMASK | IP_MF);
	}

	inline uint16_t getFragmentOffsetInBytes() {
		/*
		 * frag_off & IP_OFFMASK stores offset in number of 64-bit words
		 */
		return (ntohs(ip.frag_off) & IP_OFFMASK) * 8;
	}

	inline bool isMoreFragments() {
		return (ntohs(ip.frag_off) & IP_MF);
	}

	// disallow padding via "__attribute__ ((__packed__))"
}__attribute__ ((__packed__));

struct UDP_PSEUDOHDR {
	u_int32_t saddr;
	u_int32_t daddr;
	u_int8_t reserved;
	u_int8_t protocol;
	u_int16_t udp_length;
}__attribute__ ((__packed__));

/*
 * Complete ARP header for Ethernet with IPv4
 *
 */
struct ARP_HDR {
	struct ether_header eth;
	struct arphdr arp;
	char sourceHardwAddr[ETH_ALEN]; /* Sender hardware address.  */
	uint32_t sourceIPAddr; /* Sender IP address.  */
	char targetHardwAddr[ETH_ALEN]; /* Target hardware address.  */
	uint32_t targetIPAddr; /* Target IP address.  */
	// disallow padding via "__attribute__ ((__packed__))"
}__attribute__ ((__packed__));

struct EOB_FULL_FRAME {
	struct UDP_HDR udp;
	uint32_t finishedBurstID;
	uint32_t lastEventNum :24;
	uint8_t reserved;
}__attribute__ ((__packed__));

} /* namespace na62 */

#endif /* NETWORK_H_ */
