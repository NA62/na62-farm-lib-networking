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
#include <tbb/spin_mutex.h>
#include <tbb/mutex.h>
#include <tbb/concurrent_queue.h>
#include <tbb/concurrent_vector.h>

#include "MRP.h"
#include "../socket/EthernetUtils.h"

namespace na62 {
class Event;
namespace cream {

typedef std::pair<struct TRIGGER_RAW_HDR*, std::vector<uint16_t> > unicastTriggerAndCrateCREAMIDs_type;

class L1DistributionHandler: public AExecutable {
public:

	static void Async_RequestLKRDataMulticast(Event *event, bool zSuppressed);
	static void Async_RequestLKRDataUnicast(const Event *event, bool zSuppressed,
			const std::vector<uint16_t> crateCREAMIDs);

	static inline uint64_t GetL1TriggersSent() {
		return L1DistributionHandler::L1TriggersSent;
	}

	static inline uint64_t GetL1MRPsSent() {
		return L1DistributionHandler::L1MRPsSent;
	}

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

	/*
	 * Queues all mutlicast MRPs that should be sent. The aggregator is used to synchronize the access on that object
	 */
	static tbb::concurrent_queue<struct TRIGGER_RAW_HDR*> multicastMRPQueue;

	static struct cream::MRP_FRAME_HDR* CREAM_MulticastRequestHdr;
	static struct cream::MRP_FRAME_HDR* CREAM_UnicastRequestHdr;

	static uint64_t L1TriggersSent;
	static uint64_t L1MRPsSent;

	static boost::timer::cpu_timer MRPSendTimer_;

	static uint MAX_TRIGGERS_PER_L1MRP;
	static uint NUMBER_OF_EBS;
	static uint MIN_USEC_BETWEEN_L1_REQUESTS;
};

} /* namespace cream */
} /* namespace na62 */
#endif /* L1DISTRIBUTIONHANDLER_H_ */
