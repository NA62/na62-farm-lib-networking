/*
 * VanillaMrpSender.h
 *
 *  Created on: Sep 25, 2014
 *      Author: root
 */

#ifndef VANILLAMRPSENDER_H_
#define VANILLAMRPSENDER_H_

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <string>

#include "../socket/EthernetUtils.h"

namespace na62 {
namespace cream {

class VanillaMrpSender {
public:
	VanillaMrpSender(const std::string multicastAddr, const ushort dstPort) {
		struct ifreq if_idx;
		struct ifreq if_mac;
		int tx_len = 0;
		std::string ifName = "em1";

		/* Open RAW socket to send on */
		if ((sockfd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1) {
			perror("socket");
		}

		/* Get the index of the interface to send on */
		memset(&if_idx, 0, sizeof(struct ifreq));
		strncpy(if_idx.ifr_name, ifName.c_str(), ifName.length());
		if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
			perror("SIOCGIFINDEX");
		/* Get the MAC address of the interface to send on */
		memset(&if_mac, 0, sizeof(struct ifreq));
		strncpy(if_mac.ifr_name, ifName.c_str(), ifName.length());
		if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0)
			perror("SIOCGIFHWADDR");


		/* Index of the network device */
		socket_address.sll_ifindex = if_idx.ifr_ifindex;
		/* Address length*/
		socket_address.sll_halen = ETH_ALEN;



		/* Destination MAC */
		const uint32_t multicastGroup = inet_addr(multicastAddr.data());
		char* dstMac = EthernetUtils::GenerateMulticastMac(multicastGroup);
		memcpy(socket_address.sll_addr, dstMac, 6);
//		socket_address.sll_addr[0] = MY_DEST_MAC0;
//		socket_address.sll_addr[1] = MY_DEST_MAC1;
//		socket_address.sll_addr[2] = MY_DEST_MAC2;
//		socket_address.sll_addr[3] = MY_DEST_MAC3;
//		socket_address.sll_addr[4] = MY_DEST_MAC4;
//		socket_address.sll_addr[5] = MY_DEST_MAC5;
	}

	~VanillaMrpSender() {
	}

public:
	void send_data(const char* data, const uint dataLength) {
		if (sendto(sockfd, data, dataLength, 0,
						(struct sockaddr*) &socket_address, sizeof(struct sockaddr_ll))
						< 0)
					printf("Send failed\n");

		delete[] data;
	}

private:
	int sockfd;
	struct sockaddr_ll socket_address;
};

} /* namespace cream */
} /* namespace na62 */

#endif /* VANILLAMRPSENDER_H_ */
