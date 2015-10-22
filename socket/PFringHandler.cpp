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

#define MAX_CARD_SLOTS          32768
#define PREFETCH_BUFFERS        8
#define QUEUE_LEN               1<<16 // 32k

namespace na62 {

//pf_ring
static pfring_zc_cluster *zc;
static pfring_zc_worker *zw;
static pfring_zc_queue *inzq;
static pfring_zc_queue **inzq_arr;
static pfring_zc_queue **outzq;
static pfring_zc_buffer_pool *wsp;
static pfring_zc_pkt_buff **buffers;

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
u_int32_t NetworkHandler::numberOfThreads_;

tbb::concurrent_bounded_queue<DataContainer> NetworkHandler::asyncSendData_;

std::vector<char> NetworkHandler::myMac_;
uint_fast32_t NetworkHandler::myIP_;

NetworkHandler::NetworkHandler(std::string deviceName, uint numberOfThreads) {
	deviceName_ = deviceName;
	numberOfThreads_ = numberOfThreads;

	myMac_ = EthernetUtils::GetMacOfInterface(deviceName);
	myIP_ = EthernetUtils::GetIPOfInterface(GetDeviceName());

	asyncSendData_.set_capacity(1000);

	if (!init()) {
		LOG_ERROR<< "Unable to load pf_ring ZC" << ENDL;
		abort();
	}

}

NetworkHandler::~NetworkHandler() {
}

bool NetworkHandler::init() {
	long i;
	int cluster_id = 1; //only 1 cluster needed

	zc = pfring_zc_create_cluster(cluster_id,
			max_packet_len(deviceName_.c_str()), 0,
			MAX_CARD_SLOTS + (numberOfThreads_ * QUEUE_LEN) + numberOfThreads_
					+ PREFETCH_BUFFERS, -1, "/hugepages/" /*NULL / auto hugetlb mountpoint */
			);
	if (zc == NULL) {
		fprintf(
		stderr,
				"pfring_zc_create_cluster error [%s] Please check your hugetlb configuration\n",
				strerror(errno));
		return -1;
	}

	buffers = new pfring_zc_pkt_buff*[numberOfThreads_];
	outzq = new pfring_zc_queue*[numberOfThreads_];
	inzq_arr = new pfring_zc_queue*[1];

	for (i = 0; i < numberOfThreads_; i++) {
		buffers[i] = pfring_zc_get_packet_handle(zc);

		if (buffers[i] == NULL) {
			fprintf(stderr, "pfring_zc_get_packet_handle error\n");
			return -1;
		}
	}

	inzq = pfring_zc_open_device(zc, deviceName_.c_str(), rx_only, 0);

	if (inzq == NULL) {
		fprintf(
		stderr,
				"pfring_zc_open_device error [%s] Please check that %s is up and not already used\n",
				strerror(errno), deviceName_.c_str());
		return -1;
	}

	for (i = 0; i < numberOfThreads_; i++) {
		outzq[i] = pfring_zc_create_queue(zc, QUEUE_LEN);

		if (outzq[i] == NULL) {
			fprintf(stderr, "pfring_zc_create_queue error [%s]\n", strerror(
			errno));
			return -1;
		}
	}

	wsp = pfring_zc_create_buffer_pool(zc, PREFETCH_BUFFERS);

	if (wsp == NULL) {
		fprintf(stderr, "pfring_zc_create_buffer_pool error\n");
		return -1;
	}

	printf("Starting balancer with %d consumer threads..\n", numberOfThreads_);

	pfring_zc_distribution_func func = nullptr;

	inzq_arr[0] = inzq;

	dispatcherThread_ =
			std::thread(
					[&]() {
						zw = pfring_zc_run_balancer(inzq_arr, outzq, 1, numberOfThreads_, wsp,
								round_robin_bursts_policy, NULL /* idle callback */, func,
								(void *) ((long) numberOfThreads_), 0/*waitforpacket*/,
								-1);
						if (zw == NULL) {
							fprintf(stderr, "pfring_zc_run_balancer error [%s]\n", strerror(errno));
							abort();
							return -1;
						}
					});

	return 1;
}

int NetworkHandler::max_packet_len(const char *device) {
	int max_len;
	pfring *ring;

	ring = pfring_open(device, 1536, PF_RING_PROMISC);

	if (ring == NULL)
		return 1536;

	if (ring->dna.dna_mapped_device) {
		max_len = ring->dna.dna_dev.mem_info.rx.packet_memory_slot_len;
	} else {
		max_len = pfring_get_mtu_size(ring);
		if (max_len == 0)
			max_len = 9000 /* Jumbo */;
		max_len += 14 /* Eth */+ 4 /* VLAN */;
	}

	pfring_close(ring);

	return max_len;
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
//	for (uint i = 0; i < numberOfQueues_; i++) {
//		queueRings_[i]->get_stats(&stats);
//		LOG_INFO << i << " \t" << stats.recv << "\t" << stats.drop << "\t"
//				<< 100. * stats.drop / (stats.recv + 1.) << ENDL;
//	}
}

uint64_t NetworkHandler::GetFramesDropped() {
	uint64_t dropped = 0;
	pfring_stat stats = { 0 };
//	for (uint i = 0; i < numberOfQueues_; i++) {
//		queueRings_[i]->get_stats(&stats);
//		dropped += stats.drop;
//	}
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

uint_fast16_t NetworkHandler::GetNextFrame(uint thread_id, bool activePolling,
		u_char*& data_return) {
	pfring_zc_pkt_buff *b = buffers[thread_id];
	if (pfring_zc_recv_pkt(outzq[thread_id], &b, !activePolling) > 0) {
		data_return = pfring_zc_pkt_buff_data(b, outzq[thread_id]);
		return b->len;
	}
	return 0;
}

std::string NetworkHandler::GetDeviceName() {
	return deviceName_;
}

int NetworkHandler::SendFrameConcurrently(uint_fast16_t threadNum,
		const u_char* pkt, u_int pktLen, bool flush, bool activePoll) {
//	framesSent_.fetch_add(1, std::memory_order_relaxed);
//	/*
//	 * Check if an Ethernet trailer is needed
//	 */
//	if (pktLen < 60) {
//		/*
//		 * TODO: using tc_malloc pkt  will already be 64 Bytes long: no need to create new one! Just check it's length...
//		 */
//		char* buff = new char[60];
//		memcpy(buff, pkt, pktLen);
//
//		memset(buff + pktLen, 0, 60 - pktLen);
//		pktLen = 60;
//
//		int rc = queueRings_[threadNum]->send_packet((char*) buff, pktLen,
//				flush, activePoll);
//		delete[] buff;
//		return rc;
//	}
//
//	return queueRings_[threadNum]->send_packet((char*) pkt, pktLen, flush,
//			activePoll);
	return 0;
}

}
/* namespace na62 */
#endif
