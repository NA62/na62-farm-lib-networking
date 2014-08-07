/*
 * NetworkHandler.h
 *
 *  Created on: Jan 10, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef NetworkHandler_H_
#define NetworkHandler_H_

#include <sys/types.h>
#include <utils/AExecutable.h>
#include <atomic>
#include <cstdbool>
#include <cstdint>
#include <queue>
#include <string>
#include <vector>
#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>

#include "EthernetUtils.h"
#include <linux/pf_ring.h>

namespace na62 {
class NetworkHandler: public AExecutable {
public:
	NetworkHandler(std::string deviceName);

	static int GetNextFrame(struct pfring_pkthdr *hdr, const u_char** pkt,
			u_int pkt_len, uint8_t wait_for_incoming_packet, uint queueNumber);

	static std::string GetDeviceName();

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

	static int SendFrameConcurrently(uint16_t threadNum, char *pkt,
			u_int pktLen, bool flush = true, bool activePoll = true);

	static void PrintStats();

	static uint16_t GetNumberOfQueues();

	/**
	 * Returns the 6 byte long hardware address of the NIC the PFring object is assigned to.
	 */
	static inline std::vector<char> GetMyMac() {
		return myMac_;
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

private:
	static std::vector<char> myMac_;
	static uint32_t myIP;

	static std::atomic<uint64_t> bytesReceived_;
	static std::atomic<uint64_t> framesReceived_;
	static uint16_t numberOfQueues_;
	static std::string deviceName_;

	static tbb::spin_mutex asyncDataMutex_;
	static std::queue<DataContainer> asyncData_;

	/*
	 * The thread will send gratuitous arp requests
	 */
	void thread();
}
;

} /* namespace na62 */
#endif /* NetworkHandler_H_ */
