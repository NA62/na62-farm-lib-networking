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
#include <thread>
#include <string>
#include <vector>
#include <tbb/concurrent_queue.h>

#include "EthernetUtils.h"
#include <pfring.h>
#include <pfring_zc.h>

#define MEASURE_TIME

#include <atomic>
#ifdef MEASURE_TIME
#include <boost/timer/timer.hpp>
#include "../structs/Network.h"
#endif

namespace na62 {
class NetworkHandler: public AExecutable {
public:
	NetworkHandler(std::string deviceName, uint numberOfThreads,
			uint numberOfBuffers, void (*idelCallback)());
	virtual ~NetworkHandler();

	bool init(const uint numberOfBuffers, void (*idelCallback)());

	int max_packet_len(const char *device);

//	int32_t rr_distribution_func(pfring_zc_pkt_buff *pkt_handle,
//			pfring_zc_queue *in_queue, void *user);

	static uint_fast16_t GetNextFrame(u_int tid, bool activePolling,
			u_char*& data_return);

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
	 */
	static int DoSendQueuedFrames(uint_fast16_t threadNum);

	static int SendFrameZC(uint_fast16_t threadNum, const u_char *pkt,
			u_int pktLen, bool flush = true, bool activePoll = true);

	static void PrintStats();

	static uint_fast16_t GetNumberOfQueues();

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
		return myIP_;
	}

	static inline uint64_t GetBytesReceived() {
		return bytesReceived_;
	}

	static inline uint64_t GetFramesReceived() {
		return framesReceived_;
	}

	static inline uint64_t GetFramesSent() {
		return framesSent_;
	}

	static uint64_t GetFramesDropped();

	static uint getNumberOfEnqueuedSendFrames() {
		return asyncSendData_.size();
	}
#ifdef MEASURE_TIME
	static inline std::atomic<uint64_t>** GetPacketTimeDiffVsTime() {
		return PacketTimeDiffVsTime_;
	}

	static void ResetPacketTimeDiffVsTime() {
		for (int i = 0; i < 0x64 + 1; ++i) {
			for (int j = 0; j < 0x64 + 1; ++j) {
				PacketTimeDiffVsTime_[i][j] = 0;
			}
		}
	}
	static void StopPacketTimer() {
		PacketTime_.stop();
	}
#endif

private:
	static std::vector<char> myMac_;
	static uint_fast32_t myIP_;

	static std::atomic<uint64_t> bytesReceived_;
	static std::atomic<uint64_t> framesReceived_;
	static std::atomic<uint64_t> framesSent_;

	static std::atomic<uint64_t>** PacketTimeDiffVsTime_;

	static u_int32_t numberOfThreads_;

	static std::string deviceName_;

	static tbb::concurrent_bounded_queue<DataContainer> asyncSendData_;

	/*
	 * The thread will send gratuitous arp requests
	 */
	void thread();

#ifdef MEASURE_TIME
	static boost::timer::cpu_timer PacketTime_;
	/*
	 * Times in microseconds
	 */
	static u_int32_t PreviousPacketTime_;
#endif
}
;

} /* namespace na62 */
#endif /* NetworkHandler_H_ */
