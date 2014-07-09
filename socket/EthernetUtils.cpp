/*
 * EthernetHandler.cpp
 *
 *  Created on: Jan 11, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "EthernetUtils.h"

#include <linux/pf_ring.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>

#include "PFringHandler.h" // forward declaration

namespace na62 {

char* EthernetUtils::StringToMAC(const std::string address) {
	char *macAddress = new char[6];

	u_int mac_a, mac_b, mac_c, mac_d, mac_e, mac_f;
	if (sscanf(address.data(), "%02X:%02X:%02X:%02X:%02X:%02X", &mac_a, &mac_b, &mac_c, &mac_d, &mac_e, &mac_f) != 6) {
		printf("Invalid MAC address format (XX:XX:XX:XX:XX:XX)\n");
		return (0);
	} else {
		macAddress[0] = mac_a, macAddress[1] = mac_b, macAddress[2] = mac_c;
		macAddress[3] = mac_d, macAddress[4] = mac_e, macAddress[5] = mac_f;
	}
	return macAddress;
}

std::vector<char> EthernetUtils::GetMacOfInterface(std::string iface) {
	std::vector<char>macAddress;
	macAddress.resize(6);

	/*
	 * substring if the interface is formated like dna:ethX
	 */
	if (iface.find(':') > 0) {
		iface = iface.substr(iface.find(':') + 1);
	}

	/*
	 * substring if the interface is formated like ethX@Y
	 */
	if (iface.find('@') > 0) {
		iface = iface.substr(0, iface.find('@'));
	}

	int s;
	struct ifreq buffer;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	memset(&buffer, 0x00, sizeof(buffer));
	strcpy(buffer.ifr_name, iface.data());
	ioctl(s, SIOCGIFHWADDR, &buffer);
	close(s);

	for (int i = 0; i < 6; i++) {
		macAddress[i] = buffer.ifr_hwaddr.sa_data[i];
	}
	return macAddress;
}

u_int32_t EthernetUtils::GetIPOfInterface(std::string iface) {
	/*
	 * substring if the interface is formated like dna:ethX
	 */
	if (iface.find(':') > 0) {
		iface = iface.substr(iface.find(':') + 1);
	}

	/*
	 * substring if the interface is formated like ethX@Y
	 */
	if (iface.find('@') > 0) {
		iface = iface.substr(0, iface.find('@'));
	}

	int fd;
	struct ifreq ifr;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	ifr.ifr_addr.sa_family = AF_INET;
	strncpy(ifr.ifr_name, iface.data(), IFNAMSIZ - 1);

	ioctl(fd, SIOCGIFADDR, &ifr);
	close(fd);

	return (u_int32_t) ((struct sockaddr_in *) &ifr.ifr_addr)->sin_addr.s_addr;
}

UDP_HDR* EthernetUtils::GenerateUDP(const void* buffer, const char* dMacAddr, const uint32_t dIP, const uint16_t& sPort, const uint16_t& dPort) {
	struct UDP_HDR* hdr = (struct UDP_HDR*) buffer;

	hdr->eth.ether_type = htons(ETHERTYPE_IP);
	hdr->ip.protocol = IPPROTO_UDP;

	memcpy(hdr->eth.ether_dhost, dMacAddr, ETH_ALEN);
	memcpy(hdr->eth.ether_shost, PFringHandler::GetMyMac().data(), ETH_ALEN);

	hdr->ip.version = 4; // IP version
	hdr->ip.ihl = 5; // Internet Header Length = ihl*4B (5 = 20B is standard without additional options)
	hdr->ip.tos = 0;
	hdr->ip.id = 0; // IP ID
	hdr->ip.frag_off = htons(IP_DF);
	hdr->ip.ttl = 128; // Time to live
	hdr->ip.saddr = PFringHandler::GetMyIP(); // Source IP
	hdr->ip.daddr = dIP; // Destination IP

	hdr->udp.dest = htons(dPort);
	hdr->udp.source = htons(sPort);
	hdr->udp.check = 0;

	return hdr;
}

} /* namespace na62 */
