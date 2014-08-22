/*
 * NetworkHandler.cpp
 *
 *  Created on: Jan 10, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */
#ifdef USE_PFRING
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/lexical_cast.hpp>
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

namespace na62 {
uint16_t NetworkHandler::numberOfQueues_;

std::atomic<uint64_t> NetworkHandler::bytesReceived_(0);
std::atomic<uint64_t> NetworkHandler::framesReceived_(0);
std::string NetworkHandler::deviceName_ = "";
tbb::spin_mutex NetworkHandler::asyncDataMutex_;
std::queue<DataContainer> NetworkHandler::asyncData_;

std::vector<char> NetworkHandler::myMac_;
uint32_t NetworkHandler::myIP_;

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

	for (uint8_t i = 0; i < numberOfQueues_; i++) {
		std::string queDeviceName = deviceName;

		queDeviceName = deviceName + "@"
				+ boost::lexical_cast<std::string>((int) i);
		/*
		 * http://www.ntop.org/pfring_api/pfring_8h.html#a397061c37a91876b6b68584e2cb99da5
		 */
		pfring_set_poll_watermark(rings[i], 128);

		queueRings_[i] = new ntop::PFring(rings[i],
				(char*) queDeviceName.data(), snaplen, flags);

		if (queueRings_[i]->enable_ring() >= 0) {
			LOG(INFO)<< "Successfully opened device "
			<< queueRings_[i]->get_device_name();
		} else {
			LOG(ERROR) << "Unable to open device " << queDeviceName
			<< "! Is pf_ring not loaded or do you use quick mode and have already a socket bound to this device?!";
			exit(1);
		}
	}
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

	while (true) {
		AsyncSendFrame(std::move(arp));
		boost::this_thread::sleep(boost::posix_time::seconds(60));
	}
}

void NetworkHandler::PrintStats() {
	pfring_stat stats = { 0 };
	LOG(INFO)<< "Ring\trecv\tdrop";
	for (int i = 0; i < numberOfQueues_; i++) {
		queueRings_[i]->get_stats(&stats);
		LOG(INFO)<<i << " \t" << stats.recv << "\t" << stats.drop;
	}
}

void NetworkHandler::AsyncSendFrame(const DataContainer&& data) {
	tbb::spin_mutex::scoped_lock my_lock(asyncDataMutex_);
	asyncData_.push(std::move(data));
}

int NetworkHandler::DoSendQueuedFrames(uint16_t threadNum) {
	if (asyncDataMutex_.try_lock()) {
		if (!asyncData_.empty()) {
			const DataContainer data = std::move(asyncData_.front());
			asyncData_.pop();
			asyncDataMutex_.unlock();
			int bytes = SendFrameConcurrently(threadNum,
					(const u_char*) data.data, data.length);

			if (data.ownerMayFreeData) {
				delete[] data.data;
			}

			return bytes;
		}
	}
	asyncDataMutex_.unlock();
	return 0;
}

int NetworkHandler::GetNextFrame(struct pfring_pkthdr *hdr, const u_char** pkt,
		u_int pkt_len, uint8_t wait_for_incoming_packet, uint queueNumber) {
	int result = queueRings_[queueNumber]->get_next_packet(hdr, (char**) pkt,
			pkt_len, wait_for_incoming_packet);
	if (result == 1) {
		bytesReceived_ += hdr->len;
		framesReceived_++;
	}
	return result;
}

std::string NetworkHandler::GetDeviceName() {
	return deviceName_;
}

int NetworkHandler::SendFrameConcurrently(uint16_t threadNum, const u_char* pkt,
		u_int pktLen, bool flush, bool activePoll) {
	/*
	 * Check if an Ethernet trailer is needed
	 */
	if (pktLen < 64) {
		/*
		 * TODO: using tc_malloc pkt  will already be 64 Bytes long: no need to create new one! Just check it's length...
		 */
		char* buff = new char[64];
		memcpy(buff, pkt, pktLen);

		memset(buff + pktLen, 0, 64 - pktLen);
		pktLen = 64;

		int rc = queueRings_[threadNum % numberOfQueues_]->send_packet(
				(char*) buff, pktLen, flush, activePoll);
		delete[] buff;
		return rc;
	}

	return queueRings_[threadNum % numberOfQueues_]->send_packet((char*) pkt,
			pktLen, flush, activePoll);
}

uint16_t NetworkHandler::GetNumberOfQueues() {
	return numberOfQueues_;
}

}
/* namespace na62 */
#endif
