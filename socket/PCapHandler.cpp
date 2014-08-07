/*
 * PCapHandler.cpp
 *
 *  Created on: Aug 7, 2014
 *      Author: root
 */

#include "PCapHandler.h"

#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/thread/pthread/thread_data.hpp>
#include <linux/if_ether.h>
#include <net/ethernet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <algorithm>
#include <cstdio>
#include <iostream>
#include <vector>

namespace na62 {

std::atomic<uint64_t> PCapHandler::bytesReceived_(0);
std::atomic<uint64_t> PCapHandler::framesReceived_(0);

std::string PCapHandler::deviceName_ = "";
tbb::spin_mutex PCapHandler::asyncDataMutex_;
std::queue<DataContainer> PCapHandler::asyncData_;

pcap_t* PCapHandler::pcap_;
pcap_t* PCapHandler::pcap_sender_;

uint32_t PCapHandler::myIP;
std::vector<char> PCapHandler::PCapHandler::myMac;

PCapHandler::PCapHandler(std::string deviceName) {
	myIP = EthernetUtils::GetIPOfInterface(deviceName);
	myMac = std::move(EthernetUtils::GetMacOfInterface(deviceName));

	// Construct Ethernet header (except for source MAC address).
	// (Destination set to broadcast address, FF:FF:FF:FF:FF:FF.)
	struct ether_header header;
	header.ether_type = htons(ETH_P_ARP);
	memset(header.ether_dhost, 0xff, sizeof(header.ether_dhost));

	// Write the interface name to an ifreq structure,
	// for obtaining the source MAC and IP addresses.
	struct ifreq ifr;
	if (deviceName.length() < sizeof(ifr.ifr_name)) {
		memcpy(ifr.ifr_name, deviceName.c_str(), deviceName.length());
		ifr.ifr_name[deviceName.length()] = 0;
	} else {
		fprintf(stderr, "interface name is too long");
		exit(1);
	}

	// Open a PCAP packet capture descriptor for the specified interface.
	char pcap_errbuf[PCAP_ERRBUF_SIZE];
	pcap_errbuf[0] = '\0';
	pcap_ = pcap_open_live(deviceName.c_str(), 96, 0, 1, pcap_errbuf);

	if (pcap_errbuf[0] != '\0') {
		fprintf(stderr, "%s\n", pcap_errbuf);
	}
	if (!pcap_) {
		exit(1);
	}




	pcap_errbuf[0] = '\0';
	pcap_sender_ = pcap_open_live(deviceName.c_str(), 96, 0, 1, pcap_errbuf);

	if (pcap_errbuf[0] != '\0') {
		fprintf(stderr, "%s\n", pcap_errbuf);
	}
	if (!pcap_sender_) {
		exit(1);
	}
}

PCapHandler::~PCapHandler() {
	// Close the PCAP descriptor.
	pcap_close(pcap_);
}

void PCapHandler::thread() {
	/*
	 * Periodically send a gratuitous ARP frames
	 */
	while (true) {
		struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
				GetMyMac().data(), GetMyIP());

		AsyncSendFrame(std::move(arp));
		boost::this_thread::sleep(boost::posix_time::seconds(1));
	}
}

int PCapHandler::GetNextFrame(struct pfring_pkthdr *hdr, const u_char** pkt,
		u_int pkt_len, uint8_t wait_for_incoming_packet, uint queueNumber) {
	struct pcap_pkthdr header;

	const u_char* data = pcap_next(pcap_, &header);
	if (data == nullptr) {
		return 0;
	}

	*pkt = data;
	hdr->len = header.len;
	hdr->caplen = header.caplen;
	std::cout << "Received " << header.len << " Bytes" << std::endl;
	return header.len;
}

void PCapHandler::SendFrameConcurrently(uint16_t threadNum, const u_char *pkt,
		u_int pktLen, bool flush, bool activePoll) {
	std::cout << "Sending " << (int) pktLen << std::endl;
//	// Write the Ethernet frame to the interface.
//	if (pcap_inject(pcap_, pkt, pktLen) == -1) {
//		pcap_perror(pcap_, 0);
//		pcap_close(pcap_);
//		exit(1);
//	}

	if (pcap_sendpacket(pcap_sender_, pkt, pktLen) != 0) {
		pcap_perror(pcap_sender_, 0);
		pcap_close(pcap_sender_);
		exit(1);
	}
}

void PCapHandler::AsyncSendFrame(const DataContainer&& data) {
	tbb::spin_mutex::scoped_lock my_lock(asyncDataMutex_);
	asyncData_.push(data);
}

int PCapHandler::DoSendQueuedFrames(uint16_t threadNum) {
	asyncDataMutex_.lock();
	if (!asyncData_.empty()) {
		const DataContainer data = asyncData_.front();
		asyncData_.pop();
		asyncDataMutex_.unlock();
		SendFrameConcurrently(threadNum, (const u_char*)data.data, data.length);
		delete[] data.data;
		return data.length;
	}
	asyncDataMutex_.unlock();
	return 0;
}

void PCapHandler::PrintStats() {
}

}
/* namespace na62 */
