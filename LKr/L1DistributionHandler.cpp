/*
 * L1DistributionHandler.cpp
 *
 *  Created on: Mar 3, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "L1DistributionHandler.h"

#include <arpa/inet.h>
#include <boost/thread/pthread/thread_data.hpp>
#include <eventBuilding/Event.h>
#include <eventBuilding/SourceIDManager.h>
#include <glog/logging.h>
#include <netinet/in.h>
#include <netinet/ip.h>
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

#include "../socket/NetworkHandler.h"
#include "../structs/Network.h"

namespace na62 {
namespace cream {
tbb::concurrent_queue<TRIGGER_RAW_HDR*> L1DistributionHandler::multicastMRPQueue;

//ThreadsafeQueue<unicastTriggerAndCrateCREAMIDs_type>* L1DistributionHandler::unicastMRPWithIPsQueues;

std::vector<cream::MRP_FRAME_HDR*> L1DistributionHandler::CREAM_MulticastRequestHdrs;
cream::MRP_FRAME_HDR* L1DistributionHandler::CREAM_UnicastRequestHdr;

uint64_t L1DistributionHandler::L1TriggersSent = 0;
uint64_t L1DistributionHandler::L1MRPsSent = 0;
uint L1DistributionHandler::NUMBER_OF_EBS = 0;
uint L1DistributionHandler::MAX_TRIGGERS_PER_L1MRP = 0;
uint L1DistributionHandler::MIN_USEC_BETWEEN_L1_REQUESTS = 0;

cream::TRIGGER_RAW_HDR* generateTriggerHDR(const Event * event,
bool zSuppressed) {
	cream::TRIGGER_RAW_HDR* triggerHDR = new cream::TRIGGER_RAW_HDR();
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

void L1DistributionHandler::Async_RequestLKRDataMulticast(Event * event,
bool zSuppressed) {
	cream::TRIGGER_RAW_HDR* triggerHDR = generateTriggerHDR(event, zSuppressed);

	/*
	 * FIXME: The blocking here is quite bad as this method is called for every accepted event
	 */
	multicastMRPQueue.push(triggerHDR);
}

void L1DistributionHandler::Async_RequestLKRDataUnicast(const Event *event,
bool zSuppressed, const std::vector<uint_fast16_t> crateCREAMIDs) {
//	 cream::TRIGGER_RAW_HDR* triggerHDR = generateTriggerHDR(event,
//			zSuppressed);
//	auto pair = std::make_pair(triggerHDR, crateCREAMIDs);
//	while (!unicastMRPWithIPsQueues[threadNum].push(pair)) {
//		LOG_ERROR<<"L1DistributionHandler input queue overrun!";
//		usleep(1000);
//	}
}

void L1DistributionHandler::Initialize(uint maxTriggersPerMRP, uint numberOfEBs,
		uint minUsecBetweenL1Requests,
		std::vector<std::string> multicastGroupNames, uint sourcePort,
		uint destinationPort) {
	MAX_TRIGGERS_PER_L1MRP = maxTriggersPerMRP;
	NUMBER_OF_EBS = numberOfEBs;
	MIN_USEC_BETWEEN_L1_REQUESTS = minUsecBetweenL1Requests;

//	void* rawData =
//			operator new[](
//					numberOfEBs
//							* sizeof(ThreadsafeQueue<
//									unicastTriggerAndCrateCREAMIDs_type> ));
//	unicastMRPWithIPsQueues = static_cast<ThreadsafeQueue<
//			unicastTriggerAndCrateCREAMIDs_type>*>(rawData);
//
//	for (int i = numberOfEBs - 1; i >= 0; i--) {
//		new (&unicastMRPWithIPsQueues[i]) ThreadsafeQueue<
//				unicastTriggerAndCrateCREAMIDs_type>(100000);
//	}

	for (std::string multicastIP : multicastGroupNames) {
		cream::MRP_FRAME_HDR* hdr = new cream::MRP_FRAME_HDR();
		CREAM_MulticastRequestHdrs.push_back(hdr);

		const uint_fast32_t multicastGroup = inet_addr(multicastIP.data());

		EthernetUtils::GenerateUDP((char*) hdr,
				EthernetUtils::GenerateMulticastMac(multicastGroup),
				multicastGroup, sourcePort, destinationPort);

		hdr->MRP_HDR.ipAddress = NetworkHandler::GetMyIP();
		hdr->MRP_HDR.reserved = 0;
	}

	CREAM_UnicastRequestHdr = new cream::MRP_FRAME_HDR();
	/*
	 * TODO: The router MAC has to be set here:
	 */
	EthernetUtils::GenerateUDP((char*) CREAM_UnicastRequestHdr,
			EthernetUtils::StringToMAC("00:11:22:33:44:55"),
			0/*Will be set later*/, sourcePort, destinationPort);

	CREAM_UnicastRequestHdr->MRP_HDR.ipAddress = NetworkHandler::GetMyIP();
	CREAM_UnicastRequestHdr->MRP_HDR.reserved = 0;

//	EthernetUtils::GenerateUDP(CREAM_RequestBuff, EthernetUtils::StringToMAC("00:15:17:b2:26:fa"), "10.0.4.3", sPort, dPort);
}

void L1DistributionHandler::thread() {
	/*
	 * We need all MRPs for each CREAM  but the multicastMRPQueues stores it in the opposite order (one MRP, several CREAMs).
	 * Therefore we will fill the following map and later  produce unicast IP packets with it
	 */
//	std::vector< TRIGGER_RAW_HDR*> unicastRequestsByCrateCREAMID[SourceIDManager::NUMBER_OF_EXPECTED_CREAM_PACKETS_PER_EVENT];
	std::vector<TRIGGER_RAW_HDR*> multicastRequests;
	multicastRequests.reserve(MAX_TRIGGERS_PER_L1MRP);

	while (true) {
		/*
		 * pop some elements from the queue
		 */

		while (multicastRequests.size() != MAX_TRIGGERS_PER_L1MRP
				&& !multicastMRPQueue.empty()) {
			TRIGGER_RAW_HDR* hdr;
			while (!multicastMRPQueue.try_pop(hdr)) {
				usleep(10);
			}
			multicastRequests.push_back(hdr);
		}

		/*
		 * Now send all unicast requests
		 */
//		unicastTriggerAndCrateCREAMIDs_type unicastMRPWithCrateCREAMID;
//		for (int thread = NUMBER_OF_EBS - 1; thread != -1; thread--) { // every EB thread
//			while (unicastMRPWithIPsQueues[thread].pop(
//					unicastMRPWithCrateCREAMID)) { // every entry in the EBs queue containing MRP+list of IPs
//				for (uint_fast32_t localCREAMID : unicastMRPWithCrateCREAMID.second) { // every IP
//					/*
//					 * Add the MRP to unicastRequestsByIP with IP as key
//					 */
//					unicastRequestsByCrateCREAMID[localCREAMID].push_back(
//							unicastMRPWithCrateCREAMID.first);
//				}
//			}
//		}
		if (multicastRequests.size() > 0) {
			/*
			 * Do not send if there's still a MRP in the queue and the current list is not full
			 */
			if (NetworkHandler::getNumberOfEnqueuedSendFrames() != 0
					&& multicastRequests.size() != MAX_TRIGGERS_PER_L1MRP) {
				// sleep a bit and then fill up the multicastRequests list
				boost::this_thread::sleep(
						boost::posix_time::microsec(
								MIN_USEC_BETWEEN_L1_REQUESTS / 10));
				continue;
			}

			Async_SendMRP(multicastRequests);
		} else {
			bool didSendUnicastMRPs = false;
//			for (int i =
//					SourceIDManager::NUMBER_OF_EXPECTED_CREAM_PACKETS_PER_EVENT
//							- 1; i != -1; i--) {
//				std::vector< TRIGGER_RAW_HDR*> triggers =
//						unicastRequestsByCrateCREAMID[i];
//				if (triggers.size() > 0) {
//					Async_SendMRP(CREAM_UnicastRequestHdr, triggers);
//					didSendUnicastMRPs = true;
//				}
//			}

			if (!didSendUnicastMRPs) {
				/*
				 * In the last iteration all queues have been empty -> sleep a while
				 *
				 * The rate of MRPs should be about 100kHz/MAX_TRIGGERS_PER_L1MRP which is about 1kHz
				 * So within 1ms we will gather enough triggers for one MRP
				 */
				boost::this_thread::sleep(
						boost::posix_time::microsec(
								MIN_USEC_BETWEEN_L1_REQUESTS / 2));
			}
		}
	}
	LOG_ERROR<< "Unexpected exit of L1DistributionHandler thread" << ENDL;
	exit(1);
}

/**
 * This method uses the given dataHDR and fills it with the given triggers. Then this buffer
 * will be queued to be sent by the PacketHandlers
 */
void L1DistributionHandler::Async_SendMRP(
/*const cream::MRP_FRAME_HDR* dataHDR,*/
std::vector<TRIGGER_RAW_HDR*>& triggers) {

	uint_fast16_t offset = sizeof(cream::MRP_FRAME_HDR);

	const uint sizeOfMRP = offset
			+ sizeof(cream::TRIGGER_RAW_HDR)
					* (triggers.size() > MAX_TRIGGERS_PER_L1MRP ?
							MAX_TRIGGERS_PER_L1MRP : triggers.size());

	/*
	 * Copy tha dataHDR into a new buffer which will be sent afterwards
	 */
	char* buff = new char[sizeOfMRP];

	uint numberOfTriggers = 0;
	while (triggers.size() != 0 && numberOfTriggers != MAX_TRIGGERS_PER_L1MRP) {
		TRIGGER_RAW_HDR* trigger = triggers.back();
		triggers.pop_back();

		memcpy(reinterpret_cast<char*>(buff) + offset, trigger,
				sizeof(cream::TRIGGER_RAW_HDR));
		offset += sizeof(cream::TRIGGER_RAW_HDR);

		delete trigger;
		numberOfTriggers++;
	}

	for (auto dataHDR : CREAM_MulticastRequestHdrs) {
		char* frame = new char[offset];
		memcpy(frame, buff, offset);
		memcpy(frame, reinterpret_cast<const char*>(dataHDR),
				sizeof(cream::MRP_FRAME_HDR));

		cream::MRP_FRAME_HDR* dataHDRToBeSent = (cream::MRP_FRAME_HDR*) frame;
		dataHDRToBeSent->SetNumberOfTriggers(numberOfTriggers);

		dataHDRToBeSent->udp.ip.check = 0;
		dataHDRToBeSent->udp.ip.check = EthernetUtils::GenerateChecksum(
				(const char*) (&dataHDRToBeSent->udp.ip), sizeof(iphdr));
		dataHDRToBeSent->udp.udp.check = EthernetUtils::GenerateUDPChecksum(
				&dataHDRToBeSent->udp, dataHDRToBeSent->MRP_HDR.getSize());

		NetworkHandler::AsyncSendFrame( { frame, offset });
	}
	delete[] buff;

	L1TriggersSent += numberOfTriggers;
	L1MRPsSent++;
}
}
/* namespace cream */
} /* namespace na62 */
