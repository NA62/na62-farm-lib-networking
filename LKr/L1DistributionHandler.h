/*
 * L1DistributionHandler.h
 *
 *  Created on: Mar 3, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef L1DISTRIBUTIONHANDLER_H_
#define L1DISTRIBUTIONHANDLER_H_

#include <sys/types.h>
#include <tbb/concurrent_queue.h>
#include <utils/AExecutable.h>
#include <cstdbool>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "MRP.h"

namespace zmq {
class socket_t;
} /* namespace zmq */

namespace na62 {
class Event;
namespace cream {

typedef std::pair<TRIGGER_RAW_HDR*, std::vector<uint_fast16_t> > unicastTriggerAndCrateCREAMIDs_type;

class L1DistributionHandler: public AExecutable {
public:

	static void Async_RequestLKRDataMulticast(Event *event, bool zSuppressed);
	static void Async_RequestLKRDataUnicast(const Event *event,
	bool zSuppressed, const std::vector<uint_fast16_t> crateCREAMIDs);

	static inline uint64_t GetL1TriggersSent() {
		return L1DistributionHandler::L1TriggersSent;
	}

	static inline uint64_t GetL1MRPsSent() {
		return L1DistributionHandler::L1MRPsSent;
	}

	static void Initialize(uint maxTriggersPerMRP, uint numberOfEBs,
			uint minUsecBetweenL1Requests,
			std::vector<std::string> multicastGroupNames, uint sourcePort,
			uint destinationPort, std::string dispatcherAddress);

private:
	void thread();

	virtual void onInterruption();

	/*
	 * Will cause to send all the Triggers in <triggers> with the given <dataHDR> asynchronously
	 * @return uint_fast16_t The number of Bytes that will be sent
	 */
	static void Async_SendMRP(/*const cream::MRP_FRAME_HDR* dataHDR,*/
	std::vector<TRIGGER_RAW_HDR*>& triggers);

	/*
	 * Queues all mutlicast MRPs that should be sent. The aggregator is used to synchronize the access on that object
	 */
	static tbb::concurrent_queue<TRIGGER_RAW_HDR*> multicastMRPQueue;

	static std::vector<cream::MRP_FRAME_HDR*> CREAM_MulticastRequestHdrs;
	static cream::MRP_FRAME_HDR* CREAM_UnicastRequestHdr;

	static uint64_t L1TriggersSent;
	static uint64_t L1MRPsSent;

	static uint MAX_TRIGGERS_PER_L1MRP;
	static uint NUMBER_OF_EBS;
	static uint MIN_USEC_BETWEEN_L1_REQUESTS;

	static zmq::socket_t* dispatcherSocket_;
};

} /* namespace cream */
} /* namespace na62 */
#endif /* L1DISTRIBUTIONHANDLER_H_ */
