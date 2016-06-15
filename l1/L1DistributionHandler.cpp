/*
 * L1DistributionHandler.cpp
 *
 *  Created on: Mar 3, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */


#include <arpa/inet.h>
#include <boost/thread/pthread/thread_data.hpp>
#include <eventBuilding/Event.h>
#include <eventBuilding/SourceIDManager.h>
#include <monitoring/BurstIdHandler.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <netinet/ip.h>
//#include <src/options/MyOptions.h>
#include <netinet/udp.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>
#include <string>
#include <vector>
#include <queue>

#include "../l1/L1DistributionHandler.h"
#include "../socket/NetworkHandler.h"
#include "../structs/Network.h"

namespace na62 {
namespace l1 {
tbb::concurrent_queue<TRIGGER_RAW_HDR*> L1DistributionHandler::multicastMRPQueue;

//ThreadsafeQueue<unicastTriggerAndCrateCREAMIDs_type>* L1DistributionHandler::unicastMRPWithIPsQueues;

std::vector<MRP_FRAME_HDR*> L1DistributionHandler::L1_MulticastRequestHdrs;
MRP_FRAME_HDR* L1DistributionHandler::L1_UnicastRequestHdr;

in_port_t sport;
in_addr_t saddr;
uint_fast32_t multiIP[100];

uint64_t L1DistributionHandler::L1TriggersSent = 0;
uint64_t L1DistributionHandler::L1MRPsSent = 0;
uint L1DistributionHandler::MAX_TRIGGERS_PER_L1MRP = 0;
uint L1DistributionHandler::MIN_USEC_BETWEEN_L1_REQUESTS = 0;

l1::TRIGGER_RAW_HDR* generateTriggerHDR(const Event * event, bool zSuppressed) {
	TRIGGER_RAW_HDR* triggerHDR = new TRIGGER_RAW_HDR();
#ifdef __USE_BIG_ENDIAN_FOR_MRP
	triggerHDR->timestamp = htonl(event->getTimestamp());
	triggerHDR->fineTime = event->getFinetime();
	triggerHDR->requestZeroSuppressed = zSuppressed;
	triggerHDR->triggerTypeWord = htons(event->getTriggerTypeWord());
	triggerHDR->eventNumber = htonl(event->getEventNumber()) >> 8;
#else
	triggerHDR->timestamp = event->getTimestamp();
	triggerHDR->fineTime = event->getFinetime();
	triggerHDR->requestZeroSuppressed = zSuppressed;
	triggerHDR->triggerTypeWord = event->getTriggerTypeWord();
	triggerHDR->eventNumber = event->getEventNumber();
#endif
	return triggerHDR;
}

void L1DistributionHandler::Async_RequestL1DataMulticast(Event * event,
bool zSuppressed) {
// Don't create data requests if we are beyond end of burst and we are about to cleanup for the new burst
	if (BurstIdHandler::flushBurst()) {
		LOG_ERROR("Skipping data requests because burst is long finished");
		return;
	}
	TRIGGER_RAW_HDR* triggerHDR = generateTriggerHDR(event, zSuppressed);

	/*
	 * FIXME: The blocking here is quite bad as this method is called for every accepted event
	 */
	multicastMRPQueue.push(triggerHDR);
}

void L1DistributionHandler::Async_RequestL1DataUnicast(const Event *event,
bool zSuppressed, const std::vector<uint_fast16_t> subSourceIDIs) {
	LOG_INFO("Unicast data request not implemented!");
//	 cream::TRIGGER_RAW_HDR* triggerHDR = generateTriggerHDR(event,
//			zSuppressed);
//	auto pair = std::make_pair(triggerHDR, crateCREAMIDs);
//	while (!unicastMRPWithIPsQueues[threadNum].push(pair)) {
//		LOG_ERROR("L1DistributionHandler input queue overrun!");
//		usleep(1000);
//	}
}

void L1DistributionHandler::Initialize(uint maxTriggersPerMRP, uint minUsecBetweenL1Requests,
		std::vector<std::string> multicastGroupNames, uint sourcePort,
		uint destinationPort) {

	sport = destinationPort;
	int i = 0;

	MAX_TRIGGERS_PER_L1MRP = maxTriggersPerMRP;
	MIN_USEC_BETWEEN_L1_REQUESTS = minUsecBetweenL1Requests;

	for (std::string multicastIP : multicastGroupNames) {
		MRP_FRAME_HDR* hdr = new MRP_FRAME_HDR();
		L1_MulticastRequestHdrs.push_back(hdr);

		const uint_fast32_t multicastGroup = inet_addr(multicastIP.data());
		//saddr = multicastGroup;
		multiIP[i] = multicastGroup;
		++i;
		EthernetUtils::GenerateUDP((char*) hdr,
				EthernetUtils::GenerateMulticastMac(multicastGroup),
				multicastGroup, sourcePort, destinationPort);

		hdr->MRP_HDR.ipAddress = NetworkHandler::GetMyIP();
		hdr->MRP_HDR.reserved = 0;
	}

	L1_UnicastRequestHdr = new MRP_FRAME_HDR();
	/*
	 * TODO: The router MAC has to be set here:
	 */
	EthernetUtils::GenerateUDP((char*) L1_UnicastRequestHdr,
			EthernetUtils::StringToMAC("00:11:22:33:44:55"),
			0/*Will be set later*/, sourcePort, destinationPort);

	L1_UnicastRequestHdr->MRP_HDR.ipAddress = NetworkHandler::GetMyIP();
	L1_UnicastRequestHdr->MRP_HDR.reserved = 0;

//	EthernetUtils::GenerateUDP(CREAM_RequestBuff, EthernetUtils::StringToMAC("00:15:17:b2:26:fa"), "10.0.4.3", sPort, dPort);
}

void L1DistributionHandler::thread() {

	std::vector<TRIGGER_RAW_HDR*> multicastRequests;
	multicastRequests.reserve(MAX_TRIGGERS_PER_L1MRP);

	while (true) {

		/*
		 * pop some elements from the queue
		 */

		TRIGGER_RAW_HDR* hdr;
		while (multicastRequests.size() != MAX_TRIGGERS_PER_L1MRP && !multicastMRPQueue.empty()) {
			while (BurstIdHandler::flushBurst() && multicastMRPQueue.try_pop(hdr)) {
				LOG_ERROR("Skipping data requests because burst is long finished");
				delete hdr;
			}

			while (!multicastMRPQueue.try_pop(hdr)) {
				usleep(10);
			}
			multicastRequests.push_back(hdr);
		}


		if (multicastRequests.size() > 0) {
			/*
			 * Do not send if there's still a MRP in the queue and the current list is not full
			 */
			if (NetworkHandler::getNumberOfEnqueuedSendFrames() != 0 && multicastRequests.size() != MAX_TRIGGERS_PER_L1MRP) {
				// sleep a bit and then fill up the multicastRequests list
				boost::this_thread::sleep(boost::posix_time::microsec(MIN_USEC_BETWEEN_L1_REQUESTS / 10));
				continue;
			}

			//LOG_ERROR("Sending out data request");
			Async_SendMRP(multicastRequests);
		} else {
			/*
			 * In the last iteration all queues have been empty -> sleep a while
			 *
			 * The rate of MRPs should be about 100kHz/MAX_TRIGGERS_PER_L1MRP which is about 1kHz
			 * So within 1ms we will gather enough triggers for one MRP
			 */
			boost::this_thread::sleep(
					boost::posix_time::microsec(
							MIN_USEC_BETWEEN_L1_REQUESTS / 5));
		}
	}
}


/**
 * This method uses the given dataHDR and fills it with the given triggers. Then this buffer
 * will be queued to be sent by the PacketHandlers
 */
void L1DistributionHandler::Async_SendMRP(std::vector<TRIGGER_RAW_HDR*>& triggers) {

	uint_fast16_t offset = sizeof(MRP_FRAME_HDR);

	const uint sizeOfMRP = offset + sizeof(TRIGGER_RAW_HDR)
					* (triggers.size() > MAX_TRIGGERS_PER_L1MRP ? MAX_TRIGGERS_PER_L1MRP : triggers.size());


	/*
	 * Copy the dataHDR into a new buffer which will be sent afterwards
	 */
	char* buff = new char[sizeOfMRP];

	uint numberOfTriggers = 0;
	while (triggers.size() != 0 && numberOfTriggers != MAX_TRIGGERS_PER_L1MRP) {
		TRIGGER_RAW_HDR* trigger = triggers.back();
		triggers.pop_back();

		memcpy(reinterpret_cast<char*>(buff) + offset, trigger, sizeof(TRIGGER_RAW_HDR));
		offset += sizeof(TRIGGER_RAW_HDR);

		delete trigger;
		numberOfTriggers++;
	}
	int i = 0;
	for (auto dataHDR : L1_MulticastRequestHdrs) {
		char* frame = new char[offset];

		memcpy(frame, buff, offset);
		memcpy(frame, reinterpret_cast<const char*>(dataHDR),
				sizeof(MRP_FRAME_HDR));

		MRP_FRAME_HDR* dataHDRToBeSent = (MRP_FRAME_HDR*) frame;
		dataHDRToBeSent->SetNumberOfTriggers(numberOfTriggers);

		//dataHDRToBeSent->udp.ip.check = 0;
		//dataHDRToBeSent->udp.ip.check = EthernetUtils::GenerateChecksum((const char*) (&dataHDRToBeSent->udp.ip), sizeof(iphdr));
		//dataHDRToBeSent->udp.udp.check = EthernetUtils::GenerateUDPChecksum(&dataHDRToBeSent->udp, dataHDRToBeSent->MRP_HDR.getSize());

		saddr = multiIP[i];
		NetworkHandler::AsyncSendFrame( { frame, offset, true, sport, saddr });
		++i;
		// Don't put too many packets in the queue at the same time
		boost::this_thread::sleep(boost::posix_time::microsec(MIN_USEC_BETWEEN_L1_REQUESTS));
	}
	delete[] buff;

	L1TriggersSent += numberOfTriggers;
	L1MRPsSent++;
}
} /* namespace cream */
} /* namespace na62 */
