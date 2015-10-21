/*
 * NetworkHandler.cpp
 *
 *  Created on: Jan 10, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */
#ifdef USE_PFRING
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
#include <queue>
#include <string>
#include <vector>

#include "EthernetUtils.h"
#include "NetworkHandler.h"
#include "PFring.h"
#include <pfring.h>

#include <l0/MEP.h>
#include <l0/MEPFragment.h>

namespace na62 {
uint_fast16_t NetworkHandler::numberOfQueues_;

/*
 * TODO: use one variable per queue instead of an atomic and sum up in the monitor connector
 */
std::atomic<uint64_t> NetworkHandler::bytesReceived_(0);
std::atomic<uint64_t> NetworkHandler::framesReceived_(0);
std::atomic<uint64_t> NetworkHandler::framesSent_(0);

#ifdef MEASURE_TIME
std::atomic<uint64_t>** NetworkHandler::PacketTimeDiffVsTime_;
boost::timer::cpu_timer NetworkHandler::PacketTime_;
u_int32_t NetworkHandler::PreviousPacketTime_;
#endif

std::string NetworkHandler::deviceName_ = "";
tbb::concurrent_bounded_queue<DataContainer> NetworkHandler::asyncSendData_;

std::vector<char> NetworkHandler::myMac_;
uint_fast32_t NetworkHandler::myIP_;

static ntop::PFring ** queueRings_; // one ring per queue

NetworkHandler::NetworkHandler(std::string deviceName) {
	deviceName_ = deviceName;

	myMac_ = EthernetUtils::GetMacOfInterface(deviceName);
	myIP_ = EthernetUtils::GetIPOfInterface(GetDeviceName());

	u_int32_t flags = 0;
	flags |= PF_RING_LONG_HEADER;
	flags |= PF_RING_PROMISC;
	flags |= PF_RING_DNA_SYMMETRIC_RSS; /* Note that symmetric RSS is ignored by non-DNA drivers */

	const int snaplen = 128;

	pfring** rings = new pfring*[MAX_NUM_RX_CHANNELS];
	numberOfQueues_ = pfring_open_multichannel((char*) deviceName.data(),
			snaplen, flags, rings);

	queueRings_ = new ntop::PFring *[numberOfQueues_];

	for (uint_fast8_t i = 0; i < numberOfQueues_; i++) {
		std::string queDeviceName = deviceName;

		queDeviceName = deviceName + "@" + std::to_string((int) i);
		/*
		 * http://www.ntop.org/pfring_api/pfring_8h.html#a397061c37a91876b6b68584e2cb99da5
		 */
		pfring_set_poll_watermark(rings[i], 128);

		queueRings_[i] = new ntop::PFring(rings[i],
				(char*) queDeviceName.data(), snaplen, flags);

		if (queueRings_[i]->enable_ring() >= 0) {
			LOG_INFO<< "Successfully opened device "
			<< queueRings_[i]->get_device_name();
		} else {
			LOG_ERROR << "Unable to open device " << queDeviceName
			<< "! Is pf_ring not loaded or do you use quick mode and have already a socket bound to this device?!";
			exit(1);
		}
	}

	asyncSendData_.set_capacity(1000);
}

NetworkHandler::~NetworkHandler() {
}

void NetworkHandler::thread() {
	/*
	 * Periodically send a gratuitous ARP frame
	 */
	struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
			GetMyMac().data(), GetMyIP());
	arp.ownerMayFreeData = false;

#ifdef MEASURE_TIME
	PacketTimeDiffVsTime_ = new std::atomic<uint64_t>*[0x64 + 1];
	for (int i = 0; i < 0x64 + 1; i++) {
		PacketTimeDiffVsTime_[i] = new std::atomic<uint64_t>[0x64 + 1] { };
	}
	NetworkHandler::ResetPacketTimeDiffVsTime();
	PacketTime_.stop();
#endif

	while (true) {
		u_int16_t pktLen = arp.length;
		char buff[64];
		char* pbuff = buff;
		memcpy(pbuff, arp.data, pktLen);
		std::stringstream AAARP;
		AAARP << "ARP Gratis" << pktLen << " ";
		for (int i = 0; i < pktLen; i++)
			AAARP << std::hex << ((char) (*(pbuff + i)) & 0xFF) << " ";
//        LOG_INFO << AAARP.str() << ENDL;

		AsyncSendFrame(std::move(arp));
		boost::this_thread::sleep(boost::posix_time::seconds(60));
	}
}

void NetworkHandler::PrintStats() {
	pfring_stat stats = { 0 };
	LOG_INFO<< "Ring\trecv\tdrop\t%drop" << ENDL;
	for (uint i = 0; i < numberOfQueues_; i++) {
		queueRings_[i]->get_stats(&stats);
		LOG_INFO<< i << " \t" << stats.recv << "\t" << stats.drop << "\t"
		<< 100. * stats.drop / (stats.recv + 1.) << ENDL;
	}
}

uint64_t NetworkHandler::GetFramesDropped() {
	uint64_t dropped = 0;
	pfring_stat stats = { 0 };
	for (uint i = 0; i < numberOfQueues_; i++) {
		queueRings_[i]->get_stats(&stats);
		dropped += stats.drop;
	}
	return dropped;
}

void NetworkHandler::AsyncSendFrame(const DataContainer&& data) {
	asyncSendData_.push(std::move(data));
}

int NetworkHandler::DoSendQueuedFrames(uint_fast16_t threadNum) {
	DataContainer data;
	if (asyncSendData_.try_pop(data)) {
		int bytes = SendFrameConcurrently(threadNum, (const u_char*) data.data,
				data.length);

		if (data.ownerMayFreeData) {
			delete[] data.data;
		}

		return bytes;
	}
	return 0;
}

int NetworkHandler::GetNextFrame(struct pfring_pkthdr *hdr, char** pkt,
		u_int pkt_len, uint_fast8_t wait_for_incoming_packet,
		uint queueNumber) {
	int result = queueRings_[queueNumber]->get_next_packet(hdr, pkt, pkt_len,
			wait_for_incoming_packet);
	if (result == 1) {
#ifdef MEASURE_TIME
		if (framesReceived_ < 16 * 1e6) {
			UDP_HDR* udphdr = (UDP_HDR*) *pkt;
			const uint_fast16_t etherType = /*ntohs*/(udphdr->eth.ether_type);
			const uint_fast8_t ipProto = udphdr->ip.protocol;
			uint_fast16_t destPort = ntohs(udphdr->udp.dest);
			const uint_fast32_t dstIP = udphdr->ip.daddr;
			const char * UDPPayload = (char*) *pkt + sizeof(UDP_HDR);
			const uint_fast16_t & UdpDataLength = ntohs(udphdr->udp.len)
					- sizeof(udphdr);
//			LOG_INFO<< "destPort " << destPort << ENDL;
			if (etherType == 0x0008/*ETHERTYPE_IP*/&& ipProto == IPPROTO_UDP
					&& destPort == 58913) {
				try {
					l0::MEP* mep = new l0::MEP(UDPPayload, UdpDataLength,
							(char*) *pkt);

					if (PacketTime_.is_stopped()) {
						PacketTime_.start();
						PreviousPacketTime_ = 0;
					} else {
//			        uint PacketTimeDiffIndex = (uint) (log(PacketTime_.elapsed().wall / 1E3 - PreviousPacketTime_)/ 0.01);
						uint PacketTimeDiffIndex =
								(uint) ((PacketTime_.elapsed().wall / 1E3
										- PreviousPacketTime_) / 100.);
						if (PacketTimeDiffIndex >= 0x64)
							PacketTimeDiffIndex = 0x64;
//				    uint EventTimestampIndex = (uint) ((PacketTime_.elapsed().wall / 1E3)/2e5);
						uint EventTimestampIndex = (uint) (mep->getSourceID()
								/ 2);
						if (EventTimestampIndex >= 0x64)
							EventTimestampIndex = 0x64;
//		            LOG_INFO<< "[PacketTimeDiffIndex,EventTimeStampIndex] " << PacketTimeDiffIndex << " " << EventTimestampIndex << ENDL;
						PacketTimeDiffVsTime_[PacketTimeDiffIndex][EventTimestampIndex].fetch_add(
								1, std::memory_order_relaxed);
						PreviousPacketTime_ = PacketTime_.elapsed().wall / 1E3;
					}

				} catch (const BrokenPacketReceivedError& e) {
					return 0;
				} catch (const UnknownSourceIDFound& e) {
					return 0;
				}
			}
		}
#endif
		bytesReceived_.fetch_add(hdr->len, std::memory_order_relaxed);
		framesReceived_.fetch_add(1, std::memory_order_relaxed);
	}
	return result;
}

std::string NetworkHandler::GetDeviceName() {
	return deviceName_;
}

int NetworkHandler::SendFrameConcurrently(uint_fast16_t threadNum,
		const u_char* pkt, u_int pktLen, bool flush, bool activePoll) {
	framesSent_.fetch_add(1, std::memory_order_relaxed);
	/*
	 * Check if an Ethernet trailer is needed
	 */
	if (pktLen < 60) {
		/*
		 * TODO: using tc_malloc pkt  will already be 64 Bytes long: no need to create new one! Just check it's length...
		 */
		char* buff = new char[60];
		memcpy(buff, pkt, pktLen);

		memset(buff + pktLen, 0, 60 - pktLen);
		pktLen = 60;

		int rc = queueRings_[threadNum]->send_packet((char*) buff, pktLen,
				flush, activePoll);
		delete[] buff;
		return rc;
	}

	return queueRings_[threadNum]->send_packet((char*) pkt, pktLen, flush,
			activePoll);
}

uint_fast16_t NetworkHandler::GetNumberOfQueues() {
	return numberOfQueues_;
}

}
/* namespace na62 */
#endif
