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
#include <string>
#include <vector>
#include <tbb/concurrent_queue.h>

#include "EthernetUtils.h"
#include <pfring.h>

#define MEASURE_TIME

#include <atomic>
#ifdef MEASURE_TIME
#include <boost/timer/timer.hpp>
#include "../structs/Network.h"
#endif

#define MTU_SIZE (64)
#define MAX_NUM_MSG 1
#define BUFSIZE 65000

namespace na62 {
class NetworkHandler: public AExecutable {
public:
	NetworkHandler(std::string deviceName, int l0, int l1, int mp);
	virtual ~NetworkHandler();

	static int GetNextFrame(struct pfring_pkthdr *hdr, char** pkt,
			u_int pkt_len, uint_fast8_t wait_for_incoming_packet, uint queueNumber);


	static int GetNextFrame(char ** pkt, in_port_t &srcport, in_addr_t &srcaddr, uint_fast8_t wait_for_incoming_packet, uint queueNumber);

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

	static int SendFrameConcurrently(uint_fast16_t threadNum, const u_char *pkt,
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
	static int net_bind_udp(std::string deviceName);

	static int net_bind_udpl1(std::string deviceName);

	static void net_set_buffer_size(int cd, int max, int send);

	inline void handle_send(const boost::system::error_code& /*error*/,
			      uint_fast16_t /*bytes_transferred*/){
			  }

private:
	static std::vector<char> myMac_;
	static uint_fast32_t myIP_;
	static uint_fast16_t l0_Port_;
	static uint_fast16_t l1_Port_;
	static uint_fast16_t m_Port_;
	static int socket_;
	static int socketl1_;
	static ssize_t result_;
	static ssize_t resultl1_;
	static char buffer_[BUFSIZE];
	static char bufferl1_[BUFSIZE];
	static struct sockaddr_in senderAddr_;
	static struct sockaddr_in senderAddrl1_;
	static socklen_t senderLen_;
	static socklen_t senderLenl1_;
	static std::atomic<uint64_t> bytesReceived_;
	static std::atomic<uint64_t> framesReceived_;
	static std::atomic<uint64_t> framesSent_;
	static uint_fast16_t numberOfQueues_;
	static std::string deviceName_;

	static tbb::concurrent_bounded_queue<DataContainer> asyncSendData_;

	//static int socket_;

	/*
	 * The thread will send gratuitous arp requests
	 */
	void thread();

}
;

} /* namespace na62 */
#endif /* NetworkHandler_H_ */
