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
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
//#include <options/Options.h>
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



//std::string NetworkHandler::deviceName_ = "";
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
	numberOfQueues_ = 1;


	////////////////////////////////////L0 Port Socket///////////////////////
	/*struct sockaddr_in hostAddr;
    bzero(&hostAddr, sizeof(hostAddr));
	hostAddr.sin_family = PF_INET;
	hostAddr.sin_addr.s_addr = myIP_; //use in_addr with listen_addr or get any IP address available htonl(INADDR_ANY)
	hostAddr.sin_port = htons(MyOptions::GetInt(OPTION_L0_RECEIVER_PORT));

	//messages_->msg_hdr.msg_iov = &iovecs_[0];
	//messages_->msg_hdr.msg_iovlen = 1;
    //iovecs_->iov_base = &buffers_[0][0];
	//iovecs_->iov_len = MTU_SIZE;

	//socket
	sd = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (sd < 0) {perror("socket()");}
				//set socket RPORT option to 1
	int one = 1;
	int r = setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, (char*)&one, sizeof(one));
	if (r < 0) {perror("setsockopt(SO_REUSEPORT)");}

				//binding socket
	if (bind (sd, (struct sockaddr *)&hostAddr, sizeof(hostAddr)) < 0) {
				perror("bind()");
	}

	//net_set_buffer_size(sd, recv_buf_size, 0);
    socket_ = sd;*/



////////////////////////////////////L1 Port Socket///////////////////////

	  /*  struct sockaddr_in hostAddrl1;

		bzero(&hostAddrl1, sizeof(hostAddrl1));
		hostAddrl1.sin_family = PF_INET;
		hostAddrl1.sin_addr.s_addr = myIP_; //use in_addr with listen_addr or get any IP address available htonl(INADDR_ANY)
		hostAddrl1.sin_port = htons(MyOptions::GetInt(OPTION_CREAM_RECEIVER_PORT));
		std::cout<<hostAddrl1.sin_port<<std::endl;
		//socket
		sdl1 = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sdl1 < 0) {perror("socket()");}
					//set socket RPORT option to 1
		int onel1 = 1;
		int rl1 = setsockopt(sdl1, SOL_SOCKET, SO_REUSEPORT, (char*)&onel1, sizeof(onel1));
		if (rl1 < 0) {perror("setsockopt(SO_REUSEPORT)");}

					//binding socket
		if (bind (sdl1, (struct sockaddr *)&hostAddrl1, sizeof(hostAddrl1)) < 0) {
					perror("bind()");
		}

		//net_set_buffer_size(sdl1, recv_buf_size, 0);
    	socketl1_ = sdl1;*/




}

NetworkHandler::~NetworkHandler() {
	close(socket_);
	close(socketl1_);
}


void NetworkHandler::net_set_buffer_size(int cd, int max, int send)
{
	int i, flag;

	if (send) {
		flag = SO_SNDBUF;
	} else {
		flag = SO_RCVBUF;
	}

	int size = 0;

	for (i = 0; i < 10 && size; i++) {
		int bef;
		socklen_t size = sizeof(bef);
		if (getsockopt(cd, SOL_SOCKET, flag, &bef, &size) < 0) {
			perror("getsockopt(SOL_SOCKET)");
			break;
		}
		if (bef >= max) {
			break;
		}

		size = bef * 2;
		if (setsockopt(cd, SOL_SOCKET, flag, &size, sizeof(size)) < 0) {
			// don't log error, just break
			break;
		}
	}
}

void NetworkHandler::thread() {
	/*
	 * Periodically send a gratuitous ARP frame
	 */

	while (true) {
	//struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
	//		GetMyMac().data(), GetMyIP());
	//arp.ownerMayFreeData = false;

	//AsyncSendFrame(std::move(arp));
	//boost::this_thread::sleep(boost::posix_time::seconds(1));


	/*u_int16_t pktLen = arp.length;
	char buff[64];
	char* pbuff = buff;
	memcpy(pbuff, arp.data, pktLen);
	std::stringstream AAARP;
	AAARP << "ARP Gratis" << pktLen << " ";
	     for (int i = 0; i < pktLen; i++)
					AAARP << std::hex << ((char) (*(pbuff + i)) & 0xFF) << " ";
    std::cout << AAARP.str() << std::endl;


	*/}

}

void NetworkHandler::PrintStats() {
	//pfring_stat stats = { 0 };
	//std::cout << "Ring\trecv\tdrop\t%drop" << std::endl;
	//for (uint i = 0; i < numberOfQueues_; i++) {
		//std::cout << "MEPs recibidos: " << framesReceived_ << std::endl;
		//queueRings_[i]->get_stats(&stats);
		//std::cout << i << " \t" << stats.recv << "\t" << stats.drop << "\t"
		//<< 100. * stats.drop / (stats.recv + 1.) << std::endl;
	//}
}

uint64_t NetworkHandler::GetFramesDropped() {
	//uint64_t dropped = 0;
	//pfring_stat stats = { 0 };
	//for (uint i = 0; i < numberOfQueues_; i++) {
		//queueRings_[i]->get_stats(&stats);
	//	dropped += stats.drop;
	//}
	return 0;
}

void NetworkHandler::AsyncSendFrame(const DataContainer&& data) {

	//std::cout << "*************************ENVIAS***********************" << std::endl;
 	const char * UDPPayload = data.data;
	uint_fast16_t UdpDataLength = data.length;
	uint_fast16_t destPort = data.UDPPort;
	in_addr_t dstIP = data.UDPAddr;
	char str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(dstIP), str, INET_ADDRSTRLEN);

	if (destPort != 0) {

	 boost::asio::io_service io_service;
     boost::asio::ip::udp::resolver resolver(io_service);
     boost::asio::ip::udp::endpoint receiver_endpoint ;//= *resolver.resolve(query);
     receiver_endpoint = boost::asio::ip::udp::endpoint(boost::asio::ip::address::from_string(str), m_Port_);
     boost::asio::ip::udp::socket socket(io_service);
	 socket.open(boost::asio::ip::udp::v4());


	 socket.send_to(boost::asio::buffer(UDPPayload, UdpDataLength), receiver_endpoint);

	}

}

int NetworkHandler::DoSendQueuedFrames(uint_fast16_t threadNum) {
	DataContainer data;
	if (asyncSendData_.try_pop(data)) {
		int bytes = 0; //= SendFrameConcurrently(threadNum, (const u_char*) data.data,
				//data.length);

		if (data.ownerMayFreeData) {
			delete[] data.data;
		}
		framesSent_.fetch_add(1, std::memory_order_relaxed);

		return bytes;
	}
	return 0;
}

int NetworkHandler::SendFrameConcurrently(uint_fast16_t threadNum,
		const u_char* pkt, u_int pktLen, bool flush, bool activePoll) {
	//framesSent_.fetch_add(1, std::memory_order_relaxed);
	/*
	 * Check if an Ethernet trailer is needed
	 */
	//if (pktLen < 60) {
		/*
		 * TODO: using tc_malloc pkt  will already be 64 Bytes long: no need to create new one! Just check it's length...
		 */
	//	char* buff = new char[60];
	//	memcpy(buff, pkt, pktLen);

	//	memset(buff + pktLen, 0, 60 - pktLen);
	//	pktLen = 60;

	//	int rc = queueRings_[threadNum]->send_packet((char*) buff, pktLen,
	//			flush, activePoll);
	//	delete[] buff;
	//	return rc;
	//}

	//return queueRings_[threadNum]->send_packet((char*) pkt, pktLen, flush,
	//		activePoll);
	//return pktLen;
	return 0;
}

//int NetworkHandler::GetNextFrame(struct pfring_pkthdr *hdr, char** pkt,
//			u_int pkt_len, uint_fast8_t wait_for_incoming_packet, uint queueNumber) {
		//int rc = recvfrom(socket_, (void*) recvBuffer_, BUF_SIZE, 0, NULL, NULL);
		//if (rc == -1) {
	//		return 0;
	//	}

		//*pkt = recvBuffer_;
		//hdr->len = rc;
//		framesReceived_++;
//		bytesReceived_ += hdr->len;

		//return hdr->len;
	//}


int NetworkHandler::GetNextFrame(char **pkt, in_port_t &srcport, in_addr_t &srcaddr, uint_fast8_t wait_for_incoming_packet, uint queueNumber) {



		result_ = recvfrom(socket_, (void*) buffer_, BUFSIZE, MSG_DONTWAIT, (struct sockaddr *)&senderAddr_, &senderLen_);

		if (result_ > 0) {
			*pkt = buffer_;
			srcport = l0_Port_;//MyOptions::GetInt(OPTION_L0_RECEIVER_PORT);
			srcaddr = senderAddr_.sin_addr.s_addr;
			bytesReceived_.fetch_add(result_, std::memory_order_relaxed);
			framesReceived_.fetch_add(1, std::memory_order_relaxed);
			return result_;
		}




		resultl1_ = recvfrom(socketl1_, (void*) bufferl1_, BUFSIZE, MSG_DONTWAIT, (struct sockaddr *)&senderAddrl1_, &senderLenl1_);

		if (resultl1_ > 0) {
			*pkt = bufferl1_;
			srcport = l1_Port_;//MyOptions::GetInt(OPTION_CREAM_RECEIVER_PORT);
			srcaddr = senderAddrl1_.sin_addr.s_addr;
			bytesReceived_.fetch_add(resultl1_, std::memory_order_relaxed);
			framesReceived_.fetch_add(1, std::memory_order_relaxed);
			return resultl1_;
		}



	return 0;

}

int NetworkHandler::net_bind_udp(std::string deviceName)
{
	struct sockaddr_in hostAddr;
	bzero(&hostAddr, sizeof(hostAddr));
	hostAddr.sin_family = PF_INET;
	hostAddr.sin_addr.s_addr = myIP_; //use in_addr with listen_addr or get any IP address available htonl(INADDR_ANY)
	hostAddr.sin_port = htons(l0_Port_);//MyOptions::GetInt(OPTION_L0_RECEIVER_PORT));

	socket_ = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socket_ < 0) {
		perror("socket()");
	}

	int one = 1;
	int r = setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, (char*)&one,
			   sizeof(one));
	if (r < 0) {
		perror("setsockopt(SO_REUSEADDR)");
	}



    int r1 = setsockopt(socket_, SOL_SOCKET, SO_REUSEPORT, (char*)&one, sizeof(one));
		if (r1 < 0) {
			perror("setsockopt(SO_REUSEPORT)");

		}

	//if (setsockopt(socket_, SOL_SOCKET, SO_BINDTODEVICE, deviceName.c_str(), deviceName.length()) == -1) {
	//				perror("SO_BINDTODEVICE");
	//				close(socket_);
	//				exit(EXIT_FAILURE);
	//}

	if (bind (socket_, (struct sockaddr *)&hostAddr, sizeof(hostAddr)) < 0) {
					perror("bind()");
	}
	/* Bind to device */




	return socket_;


}

int NetworkHandler::net_bind_udpl1(std::string deviceName)
{
	struct sockaddr_in hostAddr;
	bzero(&hostAddr, sizeof(hostAddr));
	hostAddr.sin_family = PF_INET;
	hostAddr.sin_addr.s_addr = myIP_; //use in_addr with listen_addr or get any IP address available htonl(INADDR_ANY)
	hostAddr.sin_port = htons(l1_Port_);//MyOptions::GetInt(OPTION_L0_RECEIVER_PORT));

	socketl1_ = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (socketl1_ < 0) {
		perror("socketL1()");
	}

	int one = 1;
	int r = setsockopt(socketl1_, SOL_SOCKET, SO_REUSEADDR, (char*)&one,
			   sizeof(one));
	if (r < 0) {
		perror("setsockopt(SO_REUSEADDR L1)");
	}



    int r1 = setsockopt(socketl1_, SOL_SOCKET, SO_REUSEPORT, (char*)&one, sizeof(one));
		if (r1 < 0) {
			perror("setsockopt(SO_REUSEPORT L1)");

		}

	//if (setsockopt(socketl1_, SOL_SOCKET, SO_BINDTODEVICE, deviceName.c_str(), deviceName.length()) == -1) {
	//				perror("SO_BINDTODEVICE L1");
	//				close(socketl1_);
	//				exit(EXIT_FAILURE);
	//}

	if (bind (socketl1_, (struct sockaddr *)&hostAddr, sizeof(hostAddr)) < 0) {
					perror("bind L1()");
	}
	/* Bind to device */




	return socket_;


}



uint_fast16_t NetworkHandler::GetNumberOfQueues() {
	return numberOfQueues_;
}

std::string NetworkHandler::GetDeviceName() {
	return deviceName_;
}



}

#endif
