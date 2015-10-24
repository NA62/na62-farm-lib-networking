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
#define TOTAL_QUEUE_LEN         2E6

namespace na62 {

//pf_ring
pfring_zc_cluster *zc;
static pfring_zc_worker *zw;
static pfring_zc_queue *inzq;
static pfring_zc_queue *sendzq;
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

NetworkHandler::NetworkHandler(std::string deviceName, uint numberOfThreads,
		void (*idelCallback)()) {
	deviceName_ = deviceName;
	numberOfThreads_ = numberOfThreads;

	myMac_ = EthernetUtils::GetMacOfInterface(deviceName);
	myIP_ = EthernetUtils::GetIPOfInterface(deviceName);

	asyncSendData_.set_capacity(1000);

	if (!init(idelCallback)) {
		LOG_ERROR<< "Unable to load pf_ring ZC" << ENDL;
		abort();
	}

}

NetworkHandler::~NetworkHandler() {
}

static int rr = -1;

int32_t rr_distribution_func(pfring_zc_pkt_buff *pkt_handle,
		pfring_zc_queue *in_queue, void *user) {
	long num_out_queues = (long) user;
	if (++rr == num_out_queues)
		rr = 0;
	return rr;
}

bool NetworkHandler::init(void (*idelCallback)()) {
	long i;
	int cluster_id = 1; //only 1 cluster needed

	// TODO: understand how to properly compute the number of buffers (1.5 is just a random try)
	long totalNumBuffers = (2 * MAX_CARD_SLOTS) + 1.5 * TOTAL_QUEUE_LEN
			+ PREFETCH_BUFFERS;

	printf("Allocating %ul buffers (%f GB) for %i worker threads\n",
			totalNumBuffers,
			totalNumBuffers * max_packet_len(deviceName_.c_str()) * 9E-9,
			numberOfThreads_);

	zc = pfring_zc_create_cluster(cluster_id,
			max_packet_len(deviceName_.c_str()), 0, totalNumBuffers, 1,
			"/hugepages/" /*NULL / auto hugetlb mountpoint */
			);
	if (zc == NULL) {
		fprintf(
		stderr,
				"pfring_zc_create_cluster error [%s] Please check your hugetlb configuration\n",
				strerror(errno));
		return false;
	}

	outzq = (pfring_zc_queue**) calloc(numberOfThreads_,
			sizeof(pfring_zc_queue *));
	buffers = (pfring_zc_pkt_buff**) calloc(numberOfThreads_,
			sizeof(pfring_zc_pkt_buff *));
//	buffers = new pfring_zc_pkt_buff*[numberOfThreads_];
//	outzq = new pfring_zc_queue*[numberOfThreads_];
	//inzq = (pfring_zc_queue**) calloc(2, sizeof(pfring_zc_queue *));

	for (i = 0; i < numberOfThreads_; i++) {
		buffers[i] = pfring_zc_get_packet_handle(zc);

		if (buffers[i] == NULL) {
			fprintf(stderr, "pfring_zc_get_packet_handle error\n");
			return false;
		}
	}

	inzq = pfring_zc_open_device(zc, deviceName_.c_str(), rx_only, 0);
	sendzq = pfring_zc_open_device(zc, deviceName_.c_str(), tx_only, 0);

	if ((inzq == NULL) | (sendzq == NULL)) {
		fprintf(
		stderr,
				"pfring_zc_open_device error [%s] Please check that %s is up and not already used\n",
				strerror(errno), deviceName_.c_str());
		return false;
	}

	for (i = 0; i < numberOfThreads_; i++) {
		outzq[i] = pfring_zc_create_queue(zc,
		TOTAL_QUEUE_LEN / numberOfThreads_);

		if (outzq[i] == NULL) {
			fprintf(stderr, "pfring_zc_create_queue error [%s]\n", strerror(
			errno));
			return false;
		}
	}

	wsp = pfring_zc_create_buffer_pool(zc, PREFETCH_BUFFERS);

	if (wsp == NULL) {
		fprintf(stderr, "pfring_zc_create_buffer_pool error\n");
		return false;
	}

	pfring_zc_distribution_func func = rr_distribution_func;

	printf("Starting balancer with %d consumer threads..\n", numberOfThreads_);
	printf("\n\nwait: %d, bind: %d, policy: %d\n\n", 1, -1,
			round_robin_bursts_policy);

	zw = pfring_zc_run_balancer(&inzq, outzq, 1, numberOfThreads_, wsp,
			round_robin_bursts_policy, NULL /* idle callback */, func,
			(void *) ((long) numberOfThreads_), 0/*waitforpacket*/, -1);
	if (zw == NULL) {
		fprintf(stderr, "pfring_zc_run_balancer error [%s]\n", strerror(errno));
		abort();
	}
	return true;
}

int NetworkHandler::max_packet_len(const char *device) {
	pfring *ring;
	pfring_card_settings settings;

	ring = pfring_open(device, 1536,
			PF_RING_PROMISC | PF_RING_ZC_NOT_REPROGRAM_RSS);

	if (ring == NULL)
		return 1536;

	pfring_get_card_settings(ring, &settings);

	pfring_close(ring);

	return settings.max_packet_size;
}

void NetworkHandler::thread() {
	/*
	 * Periodically send a gratuitous ARP frame
	 */
	struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
			GetMyMac().data(), GetMyIP());


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
		boost::this_thread::sleep(boost::posix_time::seconds(1));
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
		int bytes = SendFrameZC(threadNum, (const u_char*) data.data,
				data.length);

		return bytes;
	}
	return 0;
}

uint_fast16_t NetworkHandler::GetNextFrame(uint thread_id, bool activePolling,
		u_char*& data_return) {
	pfring_zc_pkt_buff *b = buffers[thread_id];
	u_char* overflow = pfring_zc_pkt_buff_data(b, outzq[thread_id]) + b->len;

	if (pfring_zc_recv_pkt(outzq[thread_id], &b, activePolling) > 0) {
		memcpy(overflow, "reused", 7);
		data_return = pfring_zc_pkt_buff_data(b, outzq[thread_id]);
		printf("%p!!!!!!!!!!\n", data_return);
		std::cout << std::string((char*) overflow, 7) << std::endl;
		return b->len;
	}
	return 0;
}

std::string NetworkHandler::GetDeviceName() {
	return deviceName_;
}

int NetworkHandler::SendFrameZC(uint_fast16_t threadNum, const u_char* pkt,
		u_int pktLen, bool flush, bool activePoll) {
	pfring_zc_pkt_buff *b = buffers[threadNum];
	auto data = pfring_zc_pkt_buff_data(b, outzq[threadNum]);
	memcpy(pfring_zc_pkt_buff_data(b, outzq[threadNum]), pkt, pktLen);
	b->len = pktLen;
	while (pfring_zc_send_pkt(sendzq, &b, flush) < 0) {
		usleep(1);
	}
	return 0;
}

}
/* namespace na62 */
#endif
