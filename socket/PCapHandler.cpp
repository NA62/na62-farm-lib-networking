/*
 * NetworkHandler.cpp
 *
 *  Created on: Aug 7, 2014
 *      Author: root
 */

#include "NetworkHandler.h"

#include <asm-generic/socket.h>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/thread/pthread/thread_data.hpp>
#include <linux/if_ether.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#define BUF_SIZE 9000

namespace na62 {

std::atomic<uint64_t> NetworkHandler::bytesReceived_(0);
std::atomic<uint64_t> NetworkHandler::framesReceived_(0);

std::string NetworkHandler::deviceName_ = "";
tbb::spin_mutex NetworkHandler::asyncDataMutex_;
std::queue<DataContainer> NetworkHandler::asyncData_;

static int socket_;
static struct sockaddr_ll socket_address_;

std::vector<char> NetworkHandler::myMac_;
uint32_t NetworkHandler::myIP_;

static u_char* recvBuffer_ = new u_char[BUF_SIZE];

NetworkHandler::NetworkHandler(std::string deviceName) {
	myIP_ = EthernetUtils::GetIPOfInterface(deviceName);
	myMac_ = std::move(EthernetUtils::GetMacOfInterface(deviceName));

#define ETHER_TYPE	0x0800
	int sockopt;
	struct ifreq ifopts; /* set promiscuous mode */
	struct ifreq if_ip; /* get ip addr */

	memset(&if_ip, 0, sizeof(struct ifreq));

	/* Open PF_PACKET socket, listening for EtherType ETHER_TYPE */
	if ((socket_ = socket(PF_PACKET, SOCK_RAW, htons(ETHER_TYPE))) == -1) {
		perror("listener: socket");
		return;
	}

	/* Set interface to promiscuous mode - do we need to do this every time? */
	strncpy(ifopts.ifr_name, deviceName.c_str(), deviceName.length() - 1);
	ioctl(socket_, SIOCGIFFLAGS, &ifopts);
	ifopts.ifr_flags |= IFF_PROMISC;
	ioctl(socket_, SIOCSIFFLAGS, &ifopts);
	/* Allow the socket to be reused - incase connection is closed prematurely */
	if (setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof sockopt)
			== -1) {
		perror("setsockopt");
		close(socket_);
		exit(EXIT_FAILURE);
	}
	/* Bind to device */
	if (setsockopt(socket_, SOL_SOCKET, SO_BINDTODEVICE, deviceName.c_str(),
			deviceName.length()) == -1) {
		perror("SO_BINDTODEVICE");
		close(socket_);
		exit(EXIT_FAILURE);
	}

	/*
	 * Timeout
	 */
	struct timeval tv;
	tv.tv_sec = 0; /* 30 Secs Timeout */
	tv.tv_usec = 1000;  // Not init'ing this can cause strange errors
	if (setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv,
			sizeof(struct timeval)) == -1) {
		perror("SO_RCVTIMEO");
		close(socket_);
		exit(EXIT_FAILURE);
	}

	/*
	 * Buffer size
	 */
	int n = 1024 * 512; //experiment with it
	if (setsockopt(socket_, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) == -1) {
	  //oops... check this
	}

	struct ifreq if_idx;

	/* Get the index of the interface to send on */
	memset(&if_idx, 0, sizeof(struct ifreq));
	strncpy(if_idx.ifr_name, deviceName.c_str(), deviceName.length());
	if (ioctl(socket_, SIOCGIFINDEX, &if_idx) < 0)
		perror("SIOCGIFINDEX");

	/* Index of the network device */
	socket_address_.sll_ifindex = if_idx.ifr_ifindex;

}

NetworkHandler::~NetworkHandler() {
	close(socket_);
}

uint16_t NetworkHandler::GetNumberOfQueues() {
	return 1;
}

void NetworkHandler::thread() {
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

int NetworkHandler::GetNextFrame(struct pfring_pkthdr *hdr, const u_char** pkt,
		u_int pkt_len, uint8_t wait_for_incoming_packet, uint queueNumber) {
	int rc = recvfrom(socket_, (void*) recvBuffer_, BUF_SIZE, 0, NULL, NULL);
	if (rc == -1) {
		return 0;
	}

	*pkt = recvBuffer_;
	hdr->len = rc;
	framesReceived_++;
	bytesReceived_ += hdr->len;

	return hdr->len;
}

int NetworkHandler::SendFrameConcurrently(uint16_t threadNum,
		const u_char *pkt, u_int pktLen, bool flush, bool activePoll) {

	/* Send packet */
	if (sendto(socket_, (void*) pkt, pktLen, 0,
			(struct sockaddr*) &socket_address_, sizeof(struct sockaddr_ll))
			< 0)
		printf("Send failed\n");
return pktLen;
}

void NetworkHandler::AsyncSendFrame(const DataContainer&& data) {
	tbb::spin_mutex::scoped_lock my_lock(asyncDataMutex_);
	asyncData_.push(data);
}

int NetworkHandler::DoSendQueuedFrames(uint16_t threadNum) {
	asyncDataMutex_.lock();
	if (!asyncData_.empty()) {
		const DataContainer data = asyncData_.front();
		asyncData_.pop();
		asyncDataMutex_.unlock();
		SendFrameConcurrently(threadNum, (const u_char*) data.data,
				data.length);
		delete[] data.data;
		return data.length;
	}
	asyncDataMutex_.unlock();
	return 0;
}

void NetworkHandler::PrintStats() {
}

}
/* namespace na62 */
