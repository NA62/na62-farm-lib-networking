/*
 * PFringHandler.h
 *
 *  Created on: Jan 10, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef PFringHandler_H_
#define PFringHandler_H_

#include <boost/lexical_cast.hpp>
#include <boost/thread.hpp>
#include <atomic>
#include <vector>
#include <queue>
#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <utils/AExecutable.h>

#include "EthernetUtils.h"
#include "PFring.h" // BUGFIX: must be included AFTER any boost-header

namespace na62 {
class PFringHandler: public AExecutable {
public:
	PFringHandler(std::string deviceName);

	static inline int GetNextFrame(struct pfring_pkthdr *hdr, char** pkt,
			u_int pkt_len, uint8_t wait_for_incoming_packet, uint queueNumber) {
		int result = queueRings_[queueNumber]->get_next_packet(hdr, pkt,
				pkt_len, wait_for_incoming_packet);
		if (result == 1) {
			bytesReceived_ += hdr->len;
			framesReceived_++;
		}

		return result;
	}

	static inline std::string GetDeviceName() {
		return deviceName_;
	}

	/**
	 * This enqueues frames to be sent later by the PacketHandler threads.
	 * The data field will be deleted as soon as it has been sent
	 */
	static void AsyncSendFrame(const DataContainer&& data);

	/**
	 * Sends one frame out of the queue filled by AsyncSendFrame.
	 *
	 * This should only be called by a PacketHandler thread to ensure thread safety.
	 * TODO: This method blocks with a mutex. We should think about implementing this with ZMQ or something
	 */
	static int DoSendQueuedFrames(uint16_t threadNum);

	/**
	 * Sends the given data
	 */
	static inline void SendFrameConcurrently(uint16_t threadNum, char *pkt,
			u_int pktLen, bool flush = true, bool activePoll = true) {
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

			while (queueRings_[threadNum % numberOfQueues_]->send_packet(
					(char*) buff, pktLen, flush, activePoll) == -1) {
				usleep(100);
			}
			delete[] buff;
		}

		while (queueRings_[threadNum % numberOfQueues_]->send_packet(
				(char*) pkt, pktLen, flush, activePoll)) {
			usleep(100);
		}
	}

	/**
	 * Returns the 6 byte long hardware address of the NIC the PFring object is assigned to.
	 */
	static inline std::vector<char> GetMyMac() {
		return myMac;
	}

	/**
	 * Returns the 4 byte long IP address of the NIC the PFring object is assigned to in network byte order.
	 */
	static inline u_int32_t GetMyIP() {
		return myIP;
	}

	static inline uint64_t GetBytesReceived() {
		return bytesReceived_;
	}

	static inline uint64_t GetFramesReceived() {
		return framesReceived_;
	}

	static void PrintStats();

	static uint16_t GetNumberOfQueues() {
		return numberOfQueues_;
	}

private:
	static std::atomic<uint64_t> bytesReceived_;
	static std::atomic<uint64_t> framesReceived_;
	static ntop::PFring ** queueRings_; // one ring per queue
	static ntop::PFring * mainReceiverRing_; // ring which receives IP packets
	static uint16_t numberOfQueues_;
	static std::string deviceName_;

	static tbb::spin_mutex asyncDataMutex_;
	static std::queue<DataContainer> asyncData_;

	static uint32_t myIP;
	static std::vector<char> myMac;

	static pfring_stat GetStats() {
		pfring_stat stats = { 0 };
		pfring_stat result = { 0 };
		for (int i = 0; i < numberOfQueues_; i++) {
			queueRings_[i]->get_stats(&stats);
			result.recv += stats.recv;
			result.drop += stats.drop;
		}
		return stats;
	}

	/*
	 * The thread will send gratuitous arp requests
	 */
	void thread();
}
;

} /* namespace na62 */
#endif /* PFringHandler_H_ */
