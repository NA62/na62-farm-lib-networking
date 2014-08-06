/*
 * L1DistributionHandler.cpp
 *
 *  Created on: Mar 3, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "L1DistributionHandler.h"

#include <arpa/inet.h>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/date_time/time_duration.hpp>
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

#include "../socket/PFringHandler.h"
#include "../structs/Network.h"

namespace na62 {
namespace cream {
std::priority_queue<struct TRIGGER_RAW_HDR*,
		std::vector<struct TRIGGER_RAW_HDR*> > L1DistributionHandler::multicastMRPQueue;
tbb::spin_mutex L1DistributionHandler::multicastMRPQueue_mutex;

ThreadsafeQueue<unicastTriggerAndCrateCREAMIDs_type>* L1DistributionHandler::unicastMRPWithIPsQueues;

struct cream::MRP_FRAME_HDR* L1DistributionHandler::CREAM_MulticastRequestHdr;
struct cream::MRP_FRAME_HDR* L1DistributionHandler::CREAM_UnicastRequestHdr;

uint64_t L1DistributionHandler::L1TriggersSent = 0;
uint64_t L1DistributionHandler::L1MRPsSent = 0;
uint L1DistributionHandler::NUMBER_OF_EBS = 0;
uint L1DistributionHandler::MAX_TRIGGERS_PER_L1MRP = 0;
uint L1DistributionHandler::MIN_USEC_BETWEEN_L1_REQUESTS = 0;

std::queue<DataContainer> L1DistributionHandler::MRPQueue;
std::mutex L1DistributionHandler::sendMutex_;

boost::timer::cpu_timer L1DistributionHandler::MRPSendTimer_;

struct cream::TRIGGER_RAW_HDR* generateTriggerHDR(const Event * event,
bool zSuppressed) {
	struct cream::TRIGGER_RAW_HDR* triggerHDR =
			new struct cream::TRIGGER_RAW_HDR();
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
	struct cream::TRIGGER_RAW_HDR* triggerHDR = generateTriggerHDR(event,
			zSuppressed);

	tbb::spin_mutex::scoped_lock my_lock(multicastMRPQueue_mutex);
	multicastMRPQueue.push(triggerHDR);
}

void L1DistributionHandler::Async_RequestLKRDataUnicast(const Event *event,
		bool zSuppressed, const std::vector<uint16_t> crateCREAMIDs) {
//	struct cream::TRIGGER_RAW_HDR* triggerHDR = generateTriggerHDR(event,
//			zSuppressed);
//	auto pair = std::make_pair(triggerHDR, crateCREAMIDs);
//	while (!unicastMRPWithIPsQueues[threadNum].push(pair)) {
//		LOG(ERROR)<<"L1DistributionHandler input queue overrun!";
//		usleep(1000);
//	}
}

void L1DistributionHandler::Initialize(uint maxTriggersPerMRP, uint numberOfEBs,
		uint minUsecBetweenL1Requests, std::string multicastGroupName,
		uint sourcePort, uint destinationPort) {
	MAX_TRIGGERS_PER_L1MRP = maxTriggersPerMRP;
	NUMBER_OF_EBS = numberOfEBs;
	MIN_USEC_BETWEEN_L1_REQUESTS = minUsecBetweenL1Requests;

	void* rawData =
			operator new[](
					numberOfEBs
							* sizeof(ThreadsafeQueue<
									unicastTriggerAndCrateCREAMIDs_type> ));
	unicastMRPWithIPsQueues = static_cast<ThreadsafeQueue<
			unicastTriggerAndCrateCREAMIDs_type>*>(rawData);

	for (int i = numberOfEBs - 1; i >= 0; i--) {
		new (&unicastMRPWithIPsQueues[i]) ThreadsafeQueue<
				unicastTriggerAndCrateCREAMIDs_type>(100000);
	}

	CREAM_MulticastRequestHdr = new struct cream::MRP_FRAME_HDR();
	CREAM_UnicastRequestHdr = new struct cream::MRP_FRAME_HDR();

	const uint32_t multicastGroup = inet_addr(multicastGroupName.data());
	EthernetUtils::GenerateUDP((char*) CREAM_MulticastRequestHdr,
			EthernetUtils::GenerateMulticastMac(multicastGroup), multicastGroup,
			sourcePort, destinationPort);

	/*
	 * TODO: The router MAC has to be set here:
	 */
	EthernetUtils::GenerateUDP((char*) CREAM_UnicastRequestHdr,
			EthernetUtils::StringToMAC("00:11:22:33:44:55"),
			0/*Will be set later*/, sourcePort, destinationPort);

	CREAM_MulticastRequestHdr->MRP_HDR.ipAddress = PFringHandler::GetMyIP();
	CREAM_MulticastRequestHdr->MRP_HDR.reserved = 0;

	CREAM_UnicastRequestHdr->MRP_HDR.ipAddress = PFringHandler::GetMyIP();
	CREAM_UnicastRequestHdr->MRP_HDR.reserved = 0;

//	EthernetUtils::GenerateUDP(CREAM_RequestBuff, EthernetUtils::StringToMAC("00:15:17:b2:26:fa"), "10.0.4.3", sPort, dPort);
}

void L1DistributionHandler::thread() {
	/*
	 * We need all MRPs for each CREAM  but the multicastMRPQueues stores it in the opposite order (one MRP, several CREAMs).
	 * Therefore we will fill the following map and later  produce unicast IP packets with it
	 */
	std::vector<struct TRIGGER_RAW_HDR*> unicastRequestsByCrateCREAMID[SourceIDManager::NUMBER_OF_EXPECTED_CREAM_PACKETS_PER_EVENT];

	std::vector<struct TRIGGER_RAW_HDR*> multicastRequests;
	multicastRequests.reserve(MAX_TRIGGERS_PER_L1MRP);
	const unsigned int sleepMicros = MIN_USEC_BETWEEN_L1_REQUESTS;

	while (true) {
		/*
		 * pop some elements from the queue
		 */
		multicastMRPQueue_mutex.lock();
		while (multicastRequests.size() < MAX_TRIGGERS_PER_L1MRP
				&& !multicastMRPQueue.empty()) {
			struct TRIGGER_RAW_HDR* hdr = multicastMRPQueue.top();
			multicastMRPQueue.pop();
			multicastRequests.push_back(hdr);
		}
		multicastMRPQueue_mutex.unlock();

		/*
		 * Now send all unicast requests
		 */
		unicastTriggerAndCrateCREAMIDs_type unicastMRPWithCrateCREAMID;
		for (int thread = NUMBER_OF_EBS - 1; thread != -1; thread--) { // every EB thread
			while (unicastMRPWithIPsQueues[thread].pop(
					unicastMRPWithCrateCREAMID)) { // every entry in the EBs queue containing MRP+list of IPs
				for (uint32_t localCREAMID : unicastMRPWithCrateCREAMID.second) { // every IP
					/*
					 * Add the MRP to unicastRequestsByIP with IP as key
					 */
					unicastRequestsByCrateCREAMID[localCREAMID].push_back(
							unicastMRPWithCrateCREAMID.first);
				}
			}
		}

		if (multicastRequests.size() > 0) {
			Async_SendMRP(CREAM_MulticastRequestHdr, multicastRequests);
		} else {
			bool didSendUnicastMRPs = false;
			for (int i =
					SourceIDManager::NUMBER_OF_EXPECTED_CREAM_PACKETS_PER_EVENT
							- 1; i != -1; i--) {
				std::vector<struct TRIGGER_RAW_HDR*> triggers =
						unicastRequestsByCrateCREAMID[i];
				if (triggers.size() > 0) {
					Async_SendMRP(CREAM_UnicastRequestHdr, triggers);
					didSendUnicastMRPs = true;
				}
			}

			if (!didSendUnicastMRPs) {
				/*
				 * In the last iteration all queues have been empty -> sleep a while
				 *
				 * The rate of MRPs should be about 100kHz/MAX_TRIGGERS_PER_L1MRP which is about 1kHz
				 * So within 1ms we will gather enough triggers for one MRP
				 */
				boost::this_thread::sleep(
						boost::posix_time::microsec(sleepMicros));
//				std::this_thread::sleep_for(std::chrono::microseconds (sleepMicros));
			}
		}
	}
	std::cerr << "Unexpected exit of L1DistributionHandler thread" << std::endl;
	exit(1);
}

bool L1DistributionHandler::DoSendMRP(const uint16_t threadNum) {
	if (sendMutex_.try_lock()) {
		if (!MRPQueue.empty()) {
			if (MRPSendTimer_.elapsed().wall / 1000
					> MIN_USEC_BETWEEN_L1_REQUESTS) {

				DataContainer container = MRPQueue.front();
				MRPQueue.pop();

//				////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////				Debug printout
//				struct cream::MRP_FRAME_HDR* hdr =
//						(struct cream::MRP_FRAME_HDR*) container.data;
//
//				std::stringstream msg;
//				msg << "Sending MRP with following "
//						<< ntohs(hdr->MRP_HDR.numberOfTriggers)
//						<< " event numbers:" << std::endl;
//				uint pointer = sizeof(cream::MRP_FRAME_HDR);
//				for (int trigger = 0;
//						trigger < ntohs(hdr->MRP_HDR.numberOfTriggers);
//						trigger++) {
//					cream::TRIGGER_RAW_HDR* t =
//							(cream::TRIGGER_RAW_HDR*) (container.data + pointer);
//					pointer += sizeof(cream::TRIGGER_RAW_HDR);
//
//					msg << (ntohl(t->eventNumber) >> 8) << " \t";
//				}
//				msg << std::endl;
//				std::cout << msg.str();
//				////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

				PFringHandler::SendFrameConcurrently(threadNum, container.data,
						container.length);

				MRPSendTimer_.start();

				sendMutex_.unlock();
				return true;
			}
		}
		sendMutex_.unlock();
	}
	return false;
}

/**
 * This method uses the given dataHDR and fills it with the given triggers. Then this buffer
 * will be queued to be sent by the PacketHandlers
 */
void L1DistributionHandler::Async_SendMRP(
		const struct cream::MRP_FRAME_HDR* dataHDR,
		std::vector<struct TRIGGER_RAW_HDR*>& triggers) {

	uint16_t offset = sizeof(struct cream::MRP_FRAME_HDR);

	const uint sizeOfMRP = offset
			+ sizeof(struct cream::TRIGGER_RAW_HDR)
					* (triggers.size() > MAX_TRIGGERS_PER_L1MRP ?
							MAX_TRIGGERS_PER_L1MRP : triggers.size());

	/*
	 * Copy tha dataHDR into a new buffer which will be sent afterwards
	 */
	char* buff = new char[sizeOfMRP];
	memcpy(buff, reinterpret_cast<const char*>(dataHDR),
			sizeof(struct cream::MRP_FRAME_HDR));
	struct cream::MRP_FRAME_HDR* dataHDRToBeSent =
			(struct cream::MRP_FRAME_HDR*) buff;

	uint numberOfTriggers = 0;
	while (triggers.size() != 0 && numberOfTriggers != MAX_TRIGGERS_PER_L1MRP) {
		struct TRIGGER_RAW_HDR* trigger = triggers.back();
		triggers.pop_back();

		memcpy(reinterpret_cast<char*>(buff) + offset, trigger,
				sizeof(struct cream::TRIGGER_RAW_HDR));
		offset += sizeof(struct cream::TRIGGER_RAW_HDR);

		delete trigger;
		numberOfTriggers++;
	}

	dataHDRToBeSent->SetNumberOfTriggers(numberOfTriggers);
	dataHDRToBeSent->udp.ip.check = 0;
	dataHDRToBeSent->udp.ip.check = EthernetUtils::GenerateChecksum(
			(const char*) (&dataHDRToBeSent->udp.ip), sizeof(struct iphdr));
	dataHDRToBeSent->udp.udp.check = EthernetUtils::GenerateUDPChecksum(
			&dataHDRToBeSent->udp, dataHDRToBeSent->MRP_HDR.getSize());

	memcpy(buff, dataHDRToBeSent, offset);

	std::lock_guard<std::mutex> lock(sendMutex_);
	MRPQueue.push( { buff, offset });

	L1TriggersSent += numberOfTriggers;
	L1MRPsSent++;

}
}
/* namespace cream */
} /* namespace na62 */
