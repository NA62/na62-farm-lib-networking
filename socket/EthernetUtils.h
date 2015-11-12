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
#include <arpa/inet.h>
#include <sys/types.h>
#include <cstdbool>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

#include <structs/DataContainer.h>

#include "../structs/Network.h"

namespace na62 {

class NetworkHandler;

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
	 * cann call EthernetUtils::GetMacOfInterface(NetworkHandler::getDeviceName()); as implemented in NetworkHandler.cpp
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
		 * The we have one Zero bit and the last 23 bits of the multicast group -> &0xffff7f00
		 */
		uint32_t addr = (multicastGroup);
		uint32_t multicastIP = (addr & 0xffff7f00);

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

		DataContainer container = { data, sizeof(struct ARP_HDR), false};

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

	static inline uint16_t GenerateChecksum(const char* data, int len,
			uint sum = 0) {
		return DataContainer::GenerateChecksum(data, len, sum);
	}

	static inline uint16_t GenerateUDPChecksum(struct UDP_HDR* hdr,
			uint32_t payloadLength) {
		hdr->udp.check = 0;
		return DataContainer::Wrapsum(
				DataContainer::GenerateChecksumUnwrapped(
						(const char *) &hdr->udp,
						sizeof(udphdr), // The UDP header
						DataContainer::GenerateChecksumUnwrapped(
								(const char *) (&hdr->udp) + sizeof(udphdr),
								payloadLength, // The UDP payload
								DataContainer::GenerateChecksumUnwrapped(
										(const char *) &hdr->ip.saddr, 8,
										IPPROTO_UDP
												+ (u_int32_t) ntohs(
														hdr->udp.len))))); // the pseudo header (0x00+udp-len+sadd+daddr)
	}

	static inline bool CheckUDP(struct UDP_HDR* hdr, const char* udpPayload,
			uint32_t length) {
		return 0xFFFF
				== DataContainer::GenerateChecksumUnwrapped(
						(const char *) &hdr->udp,
						sizeof(udphdr), // The UDP header
						DataContainer::GenerateChecksumUnwrapped(udpPayload,
								length, // The UDP payload
								DataContainer::GenerateChecksumUnwrapped(
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
			sum += *((uint16_t*) data);
			data += sizeof(uint16_t);
			len--;
		}
		sum = (sum >> 16) + (sum & 0xffff);
		sum += (sum >> 16);
		return sum == 0xFFFF;
	}

	static inline std::string ipToString(int ip) {
		char buff[16];
		inet_ntop(AF_INET, &ip, buff, 16);
		return buff;
	}
};

} /* namespace na62 */
#endif /* ETHERNETHANDLER_H_ */
