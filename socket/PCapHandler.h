/*
 * PCapHandler.h
 *
 *  Created on: Aug 7, 2014
 *      Author: root
 */

#ifndef PCAPHANDLER_H_
#define PCAPHANDLER_H_

#include <pcap/pcap.h>
#include <sys/types.h>
#include <utils/AExecutable.h>
#include <cstdint>
#include <queue>
#include <string>
#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <atomic>
#include <cstdbool>
#include <linux/pf_ring.h>
#include <linux/if_packet.h>

#include "EthernetUtils.h"

namespace na62 {

class PCapHandler: public AExecutable {
public:
	PCapHandler(std::string deviceName);
	virtual ~PCapHandler();

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

	static void SendFrameConcurrently(uint16_t threadNum, const u_char *pkt,
			u_int pktLen, bool flush = true, bool activePoll = true);

	static uint16_t GetNumberOfQueues() {
		return 1;
	}

	static void PrintStats();

	static int GetNextFrame(struct pfring_pkthdr *hdr, const u_char** pkt,
			u_int pkt_len, uint8_t wait_for_incoming_packet, uint queueNumber) ;

	static inline uint64_t GetBytesReceived() {
		return bytesReceived_;
	}

	static inline uint64_t GetFramesReceived() {
		return framesReceived_;
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

private:
	static int socket_;
	static struct sockaddr_ll socket_address_;

	static std::string deviceName_;
	static tbb::spin_mutex asyncDataMutex_;
	static std::queue<DataContainer> asyncData_;

	static std::atomic<uint64_t> bytesReceived_;
	static std::atomic<uint64_t> framesReceived_;

	static uint32_t myIP;
	static std::vector<char> myMac;

	static u_char* recvBuffer_;

	void thread();
};

} /* namespace na62 */

#endif /* PCAPHANDLER_H_ */
