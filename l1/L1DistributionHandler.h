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

#include "../l1/MRP.h"
#include "../socket/EthernetUtils.h"

namespace na62 {
class Event;
namespace l1 {

class L1DistributionHandler: public AExecutable {
public:

	static void Async_RequestL1DataMulticast(Event *event, bool zSuppressed);
	static void Async_RequestL1DataUnicast(const Event *event,
			bool zSuppressed, const std::vector<uint_fast16_t> subSourceIDs);

	static inline uint64_t GetL1TriggersSent() {
		return L1DistributionHandler::L1TriggersSent;
	}

	static inline uint64_t GetL1MRPsSent() {
		return L1DistributionHandler::L1MRPsSent;
	}

	static void Initialize(uint maxTriggersPerMRP,
			uint minUsecBetweenL1Requests,
			std::vector<std::string> multicastGroupNames,
			//std::vector<std::string> unicastNames,
			uint sourcePort,
			uint destinationPort,
			std::string gatewayMAC);
	//void pingThread();
private:
	void thread();

	/*
	 * Will cause to send all the Triggers in <triggers> with the given <dataHDR> asynchronously
	 * @return uint_fast16_t The number of Bytes that will be sent
	 */
	static void Async_SendMRP(std::vector<TRIGGER_RAW_HDR*>& triggers);

	/*
	 * Queues all mutlicast MRPs that should be sent. The aggregator is used to synchronize the access on that object
	 */
	static tbb::concurrent_queue<TRIGGER_RAW_HDR*> multicastMRPQueue;

	static std::vector<MRP_FRAME_HDR*> L1_MulticastRequestHdrs;
	static std::vector<MRP_FRAME_HDR*> L1_UnicastRequestHdrs;
	static MRP_FRAME_HDR* L1_UnicastRequestHdr;

	static uint64_t L1TriggersSent;
	static uint64_t L1MRPsSent;

	static uint MAX_TRIGGERS_PER_L1MRP;
	static uint MIN_USEC_BETWEEN_L1_REQUESTS;
};

} /* namespace l1 */
} /* namespace na62 */
#endif /* L1DISTRIBUTIONHANDLER_H_ */
