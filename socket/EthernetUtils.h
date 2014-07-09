/*
 * EthernetHandler.h
 *
 *  Created on: Jan 11, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef ETHERNETHANDLER_H_
#define ETHERNETHANDLER_H_

#include <linux/if_ether.h>
#include <net/ethernet.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <cstdbool>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "../structs/Network.h"

namespace na62 {

class PFringHandler;
// forward declaration

struct DataContainer {
	char * data;
	uint16_t length;
};

class EthernetUtils {
public:

	/**
	 * Returns the 6 byte long hardware address corresponding to the given string with following format:
	 * address format (hex): XX:XX:XX:XX:XX:XX
	 */
	static char * StringToMAC(const std::string address);

	/**
	 * Returns the 6 byte long hardware address of the interface with the given name.
	 *
	 * Note: This also works with interfaces running with a pf_ring driver like "dna:ethX". So you
	 * cann call EthernetUtils::GetMacOfInterface(PFringHandler::getDeviceName()); as implemented in PFringHandler.cpp
	 */
	static std::vector<char> GetMacOfInterface(std::string iface);

	static u_int32_t GetIPOfInterface(std::string iface);

	/**
	 * Generates a IPv4/UDP packet
	 */
	static UDP_HDR* GenerateUDP(const void* data, const char* dMacAddr,
			const uint32_t dIP, const uint16_t& sPort, const uint16_t& dPort);

	static inline char* GenerateMulticastMac(const uint32_t multicastGroup) {
		/*
		 * The first three bytes of a multicast MAC are 01 00 5e
		 *
		 */
		char* macAddress = new char[6];
		macAddress[0] = 0x01;
		macAddress[1] = 0x00;
		macAddress[2] = 0x5e;

		/*
		 * The we have one Zero bit and the last 23 bits of the multicast group -> &0xffff7f
		 */
		uint32_t addr = ntohl(multicastGroup);
		uint32_t multicastIP = (addr & 0xffff7f);

		memcpy(&macAddress[0] + 3, ((char*) &multicastIP) + 1, 3);

		return macAddress;
	}

	/**
	 * Generates a Gratuitous ARP packet for Ethernet with IPv4
	 */
	static inline DataContainer GenerateGratuitousARPv4(char * sourceHardwAddr,
			uint32_t sourceIPAddr) {
		char destMac[ETH_ALEN];
		memset(destMac, 0, ETH_ALEN);
		return GenerateARPv4(sourceHardwAddr, destMac, sourceIPAddr,
				sourceIPAddr, ARPOP_REQUEST);
	}

	static inline DataContainer GenerateARPv4(char * sourceHardwAddr,
			char * targetHardwAddr, uint32_t sourceIPAddr,
			uint32_t targetIPAddr, uint16_t operation) {
		/*
		 * We must allocate at least 64 Bytes because of ethernet padding!
		 */
		char * data = new char[64];

		DataContainer container = { data, sizeof(struct ARP_HDR) };

		struct ARP_HDR* hdr = (struct ARP_HDR*) data;

		memcpy(hdr->eth.ether_dhost, targetHardwAddr, ETH_ALEN); // Broadcast destination
		memcpy(hdr->eth.ether_shost, sourceHardwAddr, ETH_ALEN);
		hdr->eth.ether_type = htons(ETHERTYPE_ARP);

		hdr->arp.ar_hrd = htons(ARPHRD_ETHER); // Hardware addr length (6 byte MAC)
		hdr->arp.ar_pro = htons(ETHERTYPE_IP); // Ether addr type (4 byte IP)

		hdr->arp.ar_hln = 6; // 6 byte MAC
		hdr->arp.ar_pln = 4; // 5 byte IP
		hdr->arp.ar_op = htons(operation); // Request

		memcpy(hdr->sourceHardwAddr, sourceHardwAddr, ETH_ALEN);
		hdr->sourceIPAddr = sourceIPAddr;

		memcpy(hdr->targetHardwAddr, targetHardwAddr, ETH_ALEN); // Broadcast destination
		hdr->targetIPAddr = targetIPAddr;

		return container;
	}

	static inline u_int32_t Wrapsum(u_int32_t sum) {
		sum = ~sum & 0xFFFF;
		return (htons(sum));
	}

	static inline uint16_t GenerateChecksum(const char* data, int len,
			uint sum = 0) {
		return Wrapsum(GenerateChecksumUnwrapped(data, len, sum));
	}

	static inline uint16_t GenerateChecksumUnwrapped(const char* data, int len,
			uint64_t sum = 0) {
		int steps = len >> 2;
		while (steps > 0) {
			sum += ntohl(*((uint32_t *) data));
			data += sizeof(uint32_t);
			--steps;
		}

		if (len % sizeof(uint32_t) != 0) {
			uint remaining = len % sizeof(uint32_t);
			uint32_t add = 0;
			while (remaining-- > 0) {
				add += *(data++); // read next byte
				add = add << 8; // move all bytes to the left
			}
			sum += ntohl(add);
		}

		while (sum > 0xffffffffULL) {
			sum = (sum & 0xffffffffULL) + (sum >> 32);
		}
		sum = (sum & 0xffff) + (sum >> 16);
		sum += (sum >> 16);
		return sum;

//		int i;
//		// Checksum all the pairs of bytes first
//		for (i = 0; i < (len & ~1U); i += 2) {
//			sum += (u_int16_t) ntohs(*((u_int16_t *) (data + i)));
//			if (sum > 0xFFFF)
//				sum -= 0xFFFF;
//		}
//		/*
//		 * If there's a single byte left over, checksum it, too.
//		 * Network byte order is big-endian, so the remaining byte is
//		 * the high byte.
//		 */
//		if (i < len) {
//			sum += data[i] << 8;
//			if (sum > 0xFFFF)
//				sum -= 0xFFFF;
//		}
//		return (sum);
	}

	static inline uint16_t GenerateUDPChecksum(struct UDP_HDR* hdr,
			uint32_t payloadLength) {
		hdr->udp.check = 0;
		return Wrapsum(
				GenerateChecksumUnwrapped((const char *) &hdr->udp,
						sizeof(udphdr), // The UDP header
						GenerateChecksumUnwrapped(
								(const char *) (&hdr->udp) + sizeof(udphdr),
								payloadLength, // The UDP payload
								GenerateChecksumUnwrapped(
										(const char *) &hdr->ip.saddr, 8,
										IPPROTO_UDP
												+ (u_int32_t) ntohs(
														hdr->udp.len))))); // the pseudo header (0x00+udp-len+sadd+daddr)
	}

	static inline bool CheckUDP(struct UDP_HDR* hdr, const char* udpPayload,
			uint32_t length) {
		return 0xFFFF
				== GenerateChecksumUnwrapped((const char *) &hdr->udp,
						sizeof(udphdr), // The UDP header
						GenerateChecksumUnwrapped(udpPayload,
								length, // The UDP payload
								GenerateChecksumUnwrapped(
										(const char *) &hdr->ip.saddr,
										8/*source and dest. address: 4 byte each*/,
										IPPROTO_UDP
												+ (u_int32_t) ntohs(
														hdr->udp.len)))); // the pseudo header (0x00+udp-len+sadd+daddr)
	}

	static inline bool CheckData(char* data, uint16_t len) {
		uint sum = 0;
		if ((len & 1) == 0) // even
			len = len >> 1;
		else
			// uneven
			len = (len >> 1) + 1;
		while (len > 0) {
			sum += *((ushort*) data);
			data += sizeof(ushort);
			len--;
		}
		sum = (sum >> 16) + (sum & 0xffff);
		sum += (sum >> 16);
		return sum == 0xFFFF;
	}
};

} /* namespace na62 */
#endif /* ETHERNETHANDLER_H_ */
