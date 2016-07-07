/*
 * UDPHandler.cpp
 *
 *  Created on: May 18, 2016
 *      Author: jcalvopi
 */
#ifdef USE_UDP

#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/thread/pthread/thread_data.hpp>
#include <glog/logging.h>
#include <linux/pf_ring.h>
#include <sys/types.h>
#include <tbb/spin_mutex.h>
#include <utils/AExecutable.h>
#include <algorithm>
#include <atomic>
#include <cstdbool>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <ostream>
#include <queue>
#include <string>
#include <vector>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <iostream>
#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "../structs/Network.h"

#include "EthernetUtils.h"
#include "NetworkHandler.h"


#include <l0/MEP.h>
#include <l0/MEPFragment.h>
#define BUFSIZE 65000

namespace na62{

/*
 * TODO: use one variable per queue instead of an atomic and sum up in the monitor connector
 */

//uint_fast16_t NetworkHandler::L0_Port;
//uint_fast16_t NetworkHandler::CREAM_Port;
std::atomic<uint64_t> NetworkHandler::bytesReceived_(0);
std::atomic<uint64_t> NetworkHandler::framesReceived_(0);
std::atomic<uint64_t> NetworkHandler::bytesReceivedl1_(0);
std::atomic<uint64_t> NetworkHandler::framesReceivedl1_(0);
std::atomic<uint64_t> NetworkHandler::framesSent_(0);
std::vector<char> NetworkHandler::myMac_;
uint_fast32_t NetworkHandler::myIP_;
uint_fast16_t NetworkHandler::l1_Port_;
uint_fast16_t NetworkHandler::l0_Port_;
uint_fast16_t NetworkHandler::m_Port_;
int NetworkHandler::socket_;
int NetworkHandler::socketl1_;
ssize_t NetworkHandler::result_;
ssize_t NetworkHandler::resultl1_;
char NetworkHandler::buffer_[BUFSIZE];
char NetworkHandler::bufferl1_[BUFSIZE];
struct sockaddr_in NetworkHandler::senderAddr_;
struct sockaddr_in NetworkHandler::senderAddrl1_;
socklen_t NetworkHandler::senderLen_;
socklen_t NetworkHandler::senderLenl1_;
uint_fast16_t NetworkHandler::numberOfQueues_;
std::string NetworkHandler::deviceName_;

///std::string NetworkHandler::deviceName_ = "";
tbb::concurrent_bounded_queue<DataContainer> NetworkHandler::asyncSendData_;

NetworkHandler::NetworkHandler(std::string deviceName, int l0_Port, int l1_Port, int m_Port){

	myMac_ = EthernetUtils::GetMacOfInterface(deviceName);
	myIP_ = EthernetUtils::GetIPOfInterface(deviceName);
	l0_Port_ = l0_Port;
	l1_Port_ = l1_Port;
	m_Port_ = m_Port;
	deviceName_ = deviceName;
	senderLen_ = sizeof(senderAddr_);
	senderLenl1_ = sizeof(senderAddrl1_);
	numberOfQueues_ = 6;

}

NetworkHandler::~NetworkHandler() {
	//close(sd);
	//close(sdl1);
}



void NetworkHandler::thread() {
	/*
	 * Periodically send a gratuitous ARP frame
	 */

	while (true) {


	}

}

void NetworkHandler::PrintStatsL0() {

	LOG_INFO( "L0 MEPs received: " << GetFramesL0() );
	LOG_INFO( "L0 Bytes received: " << GetBytesL0() );
	LOG_INFO( "L1 Requests: " << GetFramesRequestSent());

}

void NetworkHandler::PrintStatsL1() {

	LOG_INFO( "L1 MEPs received: " << GetFramesL1() );
	LOG_INFO( "L1 Bytes received: " << GetBytesL1() );

}

uint64_t NetworkHandler::GetFramesDropped() {

	return 0;
}

void NetworkHandler::AsyncSendFrame(const DataContainer&& data) {

	const char * UDPPayload = data.data;
	uint_fast16_t UdpDataLength = data.length;
	uint_fast16_t destPort = data.UDPPort;
	in_addr_t dstIP = data.UDPAddr;
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(dstIP), str, INET_ADDRSTRLEN);

	//if (destPort != 0) {

		boost::asio::io_service io_service;
		boost::asio::ip::udp::resolver resolver(io_service);
		boost::asio::ip::udp::endpoint receiver_endpoint ;//= *resolver.resolve(query);
		receiver_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(str), m_Port_);
		boost::asio::ip::udp::socket socket(io_service);
		socket.open(boost::asio::ip::udp::v4());
		socket.send_to(boost::asio::buffer(UDPPayload, UdpDataLength), receiver_endpoint);
		framesSent_.fetch_add(1, std::memory_order_relaxed);

	//}

}



int NetworkHandler::GetNextFrame(char **pkt, in_port_t &srcport, in_addr_t &srcaddr, uint_fast8_t wait_for_incoming_packet, uint queueNumber, int sd, bool &timeout) {

	char buf[BUFSIZE];
	ssize_t result = 0;
	result = recvfrom(sd, (void*) buf, BUFSIZE, 0, (struct sockaddr *)&senderAddr_, &senderLen_);

	if (result == -1) {
				if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
				    timeout = true;
					//LOG_INFO("Timeout");
					return 0;
				}
				LOG_ERROR("recvmmsg()");

	}

	//if (result > 0) {

		*pkt = buf;
		srcport = l0_Port_;//MyOptions::GetInt(OPTION_L0_RECEIVER_PORT);
		srcaddr = senderAddr_.sin_addr.s_addr;
		bytesReceived_.fetch_add(result, std::memory_order_relaxed);
		framesReceived_.fetch_add(1, std::memory_order_relaxed);
		return result;
	//}
	//return 0;

}

int NetworkHandler::GetNextFrameL1(char **pktl1, in_port_t &srcportl1, in_addr_t &srcaddrl1, uint_fast8_t wait_for_incoming_packetl1, uint queueNumberl1, int sdl1, bool &timeoutl1) {

	char buf[BUFSIZE];
	ssize_t resultl1 = 0;
	resultl1 = recvfrom(sdl1, (void*) buf, BUFSIZE, 0, (struct sockaddr *)&senderAddrl1_, &senderLenl1_);

	if (resultl1 == -1) {
					if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
						timeoutl1 = true;
						//LOG_INFO("Timeout");
						return 0;
					}
					LOG_ERROR("recvmmsg()");
	}

	//if (resultl1 > 0) {
	   // LOG_ERROR("Received");
		*pktl1 = buf;
		srcportl1 = l1_Port_;//MyOptions::GetInt(OPTION_CREAM_RECEIVER_PORT);
		srcaddrl1 = senderAddrl1_.sin_addr.s_addr;
		bytesReceivedl1_.fetch_add(resultl1, std::memory_order_relaxed);
		framesReceivedl1_.fetch_add(1, std::memory_order_relaxed);
		return resultl1;
	//}
	//return 0;


}

int NetworkHandler::net_bind_udp()
{

	//struct sockaddr_ll hostAddr;
	//struct ifreq ifr;
	struct sockaddr_in hostAddr;
	bzero(&hostAddr, sizeof(hostAddr));



	hostAddr.sin_family = AF_INET;
	hostAddr.sin_addr.s_addr = myIP_; //use in_addr with listen_addr or get any IP address available htonl(INADDR_ANY)
	hostAddr.sin_port = htons(l0_Port_);//MyOptions::GetInt(OPTION_L0_RECEIVER_PORT));

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fd < 0) {
		LOG_ERROR("socket()");
	}

	//strcpy(ifr.ifr_name, "dna0");
	//if (ioctl(fd ,SIOCGIFINDEX, &ifr) < 0){
	//	perror("device index");
	//}

	int one = 1;
	int n = 1024 * 1024;
	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) == -1) {
		LOG_ERROR("setting buffer");
	}

	//hostAddr.sll_family = AF_PACKET;
	//hostAddr.sll_protocol = htons(ETH_P_IP);
	//hostAddr.sll_ifindex = ifr.ifr_ifindex;
	//*hostAddr.sll_addr = *ifr.ifr_addr.sa_data;

	int r1 = setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
	if (r1 < 0) {
		LOG_ERROR("setsockopt(SO_REUSEPORT)");

	}

	struct timeval tv;

	tv.tv_sec = 1;
	tv.tv_usec = 500000;

	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval))==-1){
		LOG_ERROR("setting timeout L0");
	}


	if (bind (fd, (struct sockaddr *)&hostAddr, sizeof(hostAddr)) < 0) {
		LOG_ERROR("bind()");
	}

	return fd;


}

int NetworkHandler::net_bind_udpl1()
{
	struct sockaddr_in hostAddr;
	bzero(&hostAddr, sizeof(hostAddr));
	hostAddr.sin_family = AF_INET;
	hostAddr.sin_addr.s_addr = myIP_; //use in_addr with listen_addr or get any IP address available htonl(INADDR_ANY)
	hostAddr.sin_port = htons(l1_Port_);//MyOptions::GetInt(OPTION_L0_RECEIVER_PORT));

	int fdl1 = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (fdl1 < 0) {
		LOG_ERROR("socket L1()");
	}

	int one = 1;
	//int r = setsockopt(socketl1_, SOL_SOCKET, SO_REUSEADDR, (char*)&one,
	//		   sizeof(one));
	//if (r < 0) {
	//	perror("setsockopt(SO_REUSEADDR L1)");
	//}



	int r1 = setsockopt(fdl1, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one));
	if (r1 < 0) {
		LOG_ERROR("setsockopt(SO_REUSEPORT L1)");

	}

	int n = 1024 * 1024;
	if (setsockopt(fdl1, SOL_SOCKET, SO_RCVBUF, &n, sizeof(n)) == -1) {
		LOG_ERROR("setting buffer L1");
	}

	//if (setsockopt(socketl1_, SOL_SOCKET, SO_BINDTODEVICE, deviceName.c_str(), deviceName.length()) == -1) {
	//				perror("SO_BINDTODEVICE L1");
	//				close(socketl1_);
	//				exit(EXIT_FAILURE);
	//}

	struct timeval tv;

		tv.tv_sec = 1;
		tv.tv_usec = 500000;

	if (setsockopt(fdl1, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv,sizeof(struct timeval)) == -1) {
		LOG_ERROR("setting timeout L1");
	}

	if (bind (fdl1, (struct sockaddr *)&hostAddr, sizeof(hostAddr)) < 0) {
		LOG_ERROR("bind L1()");
	}

	return fdl1;


}



uint_fast16_t NetworkHandler::GetNumberOfQueues() {
	return numberOfQueues_;
}

std::string NetworkHandler::GetDeviceName() {
	return deviceName_;
}



}

#endif
