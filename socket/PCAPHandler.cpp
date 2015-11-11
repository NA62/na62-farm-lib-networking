/*
 * NetworkHandler.cpp
 *
 *  Created on: Aug 7, 2014
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#ifdef USE_PCAP
#include "NetworkHandler.h"

#include <asm-generic/socket.h>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/thread/pthread/thread_data.hpp>
#include <linux/if_ether.h>
#include <net/if.h>
#include <pcap.h>
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
	std::atomic<uint64_t> NetworkHandler::framesSent_(0);

#ifdef MEASURE_TIME
	std::atomic<uint64_t>* NetworkHandler::PacketTimeDiffVsTime_ = (std::atomic<uint64_t>*) malloc(10000);

	boost::timer::cpu_timer NetworkHandler::PacketTime_;
	u_int32_t NetworkHandler::PreviousPacketTime_;

#endif

	std::string NetworkHandler::deviceName_ = "";
	tbb::concurrent_bounded_queue<DataContainer> NetworkHandler::asyncSendData_;

	static pcap_t *handle;
	char* dev, errbuff[PCAP_ERRBUF_SIZE];
	bpf_u_int32 mask;		/* Our netmask */
	bpf_u_int32 net;		/* Our IP */
	std::vector<char> NetworkHandler::myMac_;
	uint_fast32_t NetworkHandler::myIP_;

	const u_char* recvBuffer_ = new u_char[BUF_SIZE];

	NetworkHandler::NetworkHandler(std::string deviceName) {
		myIP_ = EthernetUtils::GetIPOfInterface(deviceName);
		myMac_ = std::move(EthernetUtils::GetMacOfInterface(deviceName));

#define ETHER_TYPE	0x0800

		dev = pcap_lookupdev(errbuff);
		LOG_INFO << "Starting PCAP on device: " << dev << ENDL;
		if (dev == NULL) {
			LOG_ERROR << "Couldn't open device: " << errbuff << ENDL;
		}
		if (pcap_lookupnet(dev, &net, &mask, errbuff) == -1) {
			fprintf(stderr, "Couldn't get netmask for device %s: %s\n", dev, errbuff);
			net = 0;
			mask = 0;
		}
		handle = pcap_open_live(dev, BUF_SIZE, 0, 0, errbuff);
		if (handle == NULL) {
			LOG_ERROR << "Couldn't get handle: " << errbuff << ENDL;
		}


	}

	NetworkHandler::~NetworkHandler() {
		pcap_close(handle);
	}

	uint_fast16_t NetworkHandler::GetNumberOfQueues() {
		return 1;
	}

	void NetworkHandler::thread() {
		/*
		 * Periodically send a gratuitous ARP frames
		 */
		NetworkHandler::ResetPacketTimeDiffVsTime();
		LOG_INFO << "NHTHREAD" << ENDL;
		NetworkHandler::PacketTime_.stop();
		while (true) {
			struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
					GetMyMac().data(), GetMyIP());
			arp.ownerMayFreeData = false;

			AsyncSendFrame(std::move(arp));
			boost::this_thread::sleep(boost::posix_time::seconds(1));
		}
	}

	int NetworkHandler::GetNextFrame(struct pfring_pkthdr *hdr, char** pkt,
			u_int pkt_len, uint_fast8_t wait_for_incoming_packet, uint queueNumber) {
		LOG_INFO << "Ready to receive" << ENDL;
		struct pcap_pkthdr header;
		pcap_pkthdr* headerptr = &header;
		int res =  pcap_next_ex(handle, &headerptr, &recvBuffer_);
		if (res == -1 ){
			pcap_perror(handle,0);
			LOG_ERROR << "Couldn't grab packet: " << ENDL;

		}
		hdr->len = header.len;
		hdr->ts = header.ts;
		hdr->caplen = header.caplen;
		*pkt = (char*) recvBuffer_;
		framesReceived_++;
		bytesReceived_ += hdr->len;

		return hdr->len;
	}

	int NetworkHandler::SendFrameConcurrently(uint_fast16_t threadNum, const u_char *pkt,
			u_int pktLen, bool flush, bool activePoll) {

		framesSent_.fetch_add(1, std::memory_order_relaxed);
		/* Send packet */
		int res = pcap_inject(handle, pkt, pktLen);
		if(res == -1){
			pcap_perror(handle,0);
			LOG_ERROR << "Couldn't send frame " << ENDL;
		}

		return res;
	}

	uint64_t NetworkHandler::GetFramesDropped() {
		return 0;
	}

	void NetworkHandler::AsyncSendFrame(const DataContainer&& data) {
		asyncSendData_.push(std::move(data));
	}

	int NetworkHandler::DoSendQueuedFrames(uint_fast16_t threadNum) {
		DataContainer data;
		if (asyncSendData_.try_pop(data)) {
//			LOG_INFO << "###############sending#############" << ENDL;
			int bytes = SendFrameConcurrently(threadNum, (const u_char*) data.data,
					data.length);

			if (data.ownerMayFreeData) {
				delete[] data.data;
			}
			framesSent_.fetch_add(1, std::memory_order_relaxed);

			return bytes;
		}
//		LOG_INFO << "---------------sending---------------" << ENDL;
		return 0;
	}

	void NetworkHandler::PrintStats() {
	}

}
/* namespace na62 */
#endif
