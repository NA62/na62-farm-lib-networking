/*
 * L1DistributionHandler.h
 *
 *  Created on: Mar 3, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef L1DISTRIBUTIONHANDLER_H_
#define L1DISTRIBUTIONHANDLER_H_

#include <atomic>
#include <cstdbool>
#include <cstdint>
#include <utility>
#include <queue>
#include <boost/thread.hpp>
#include <boost/timer/timer.hpp>
#include <mutex>
#include <utils/AExecutable.h>
#include <utils/ThreadsafeQueue.h>

#include "MRP.h"
#include "../socket/EthernetUtils.h"

namespace na62 {
class Event;
namespace cream {

typedef std::pair<struct TRIGGER_RAW_HDR*, std::vector<uint16_t> > unicastTriggerAndCrateCREAMIDs_type;

class L1DistributionHandler: public AExecutable {
public:

	static void Async_RequestLKRDataMulticast(const uint16_t threadNum,
			Event *event, bool zSuppressed);
	static void Async_RequestLKRDataUnicast(const uint16_t threadNum,
			const Event *event, bool zSuppressed,
			const std::vector<uint16_t> crateCREAMIDs);

	static inline uint64_t GetL1TriggersSent() {
		return L1DistributionHandler::L1TriggersSent;
	}

	static inline uint64_t GetL1MRPsSent() {
		return L1DistributionHandler::L1MRPsSent;
	}

	/*
	 * Allows an EB to stop the MRP transmission
	 */
	static inline void PauseSending() {
		paused = true;
	}

	/*
	 * Allows an EB to restart the MRP transmission
	 */
	static inline void ResumeSending() {
		paused = false;
	}

	static inline bool SendingIsPaused() {
		return paused;
	}

	/*
	 * Should be called by the PacketHandler threads to actually send the queued MRPs
	 * @return <true> in case a frame has been sent (and time has passed therefore)
	 */
	static bool DoSendMRP(const uint16_t threadNum);

	static void Initialize(uint maxTriggersPerMRP, uint numberOfEBs,
			uint minUsecBetweenL1Requests, std::string multicastGroupName,
			uint sourcePort, uint destinationPort);

private:
	void thread();

	/*
	 * Will cause to send all the Triggers in <triggers> with the given <dataHDR> asynchronously
	 * @return uint16_t The number of Bytes that will be sent
	 */
	static void Async_SendMRP(const struct cream::MRP_FRAME_HDR* dataHDR,
			std::vector<struct TRIGGER_RAW_HDR*>& triggers);

	static ThreadsafeQueue<struct TRIGGER_RAW_HDR*>* multicastMRPQueues;
	static ThreadsafeQueue<unicastTriggerAndCrateCREAMIDs_type>* unicastMRPWithIPsQueues;

	static struct cream::MRP_FRAME_HDR* CREAM_MulticastRequestHdr;
	static struct cream::MRP_FRAME_HDR* CREAM_UnicastRequestHdr;

	static uint64_t L1TriggersSent;
	static uint64_t L1MRPsSent;

	static bool paused;

	static std::queue<DataContainer> MRPQueues;

	static std::mutex sendMutex_;

	static boost::timer::cpu_timer MRPSendTimer_;

	static uint MAX_TRIGGERS_PER_L1MRP;
	static uint NUMBER_OF_EBS;
	static uint MIN_USEC_BETWEEN_L1_REQUESTS;
};

} /* namespace cream */
} /* namespace na62 */
#endif /* L1DISTRIBUTIONHANDLER_H_ */
