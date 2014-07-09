/*
 * PFringHandler.cpp
 *
 *  Created on: Jan 10, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "PFringHandler.h"

#include <glog/logging.h>

namespace na62 {
ntop::PFring ** PFringHandler::queueRings_;
uint16_t PFringHandler::numberOfQueues_;

std::atomic<uint64_t> PFringHandler::bytesReceived_(0);
std::atomic<uint64_t> PFringHandler::framesReceived_(0);
std::string PFringHandler::deviceName_ = "";
boost::mutex PFringHandler::sendMutex_;

PFringHandler::PFringHandler(std::string deviceName) {
	deviceName_ = deviceName;
	u_int32_t flags = 0;
	flags |= PF_RING_LONG_HEADER;
	flags |= PF_RING_PROMISC;
	flags |= PF_RING_DNA_SYMMETRIC_RSS; /* Note that symmetric RSS is ignored by non-DNA drivers */

	const int snaplen = 128;

	pfring** rings = new pfring*[MAX_NUM_RX_CHANNELS];
	numberOfQueues_ = pfring_open_multichannel((char*) deviceName.data(),
			snaplen, flags, rings);

	queueRings_ = new ntop::PFring *[numberOfQueues_];

	for (uint8_t i = 0; i < numberOfQueues_; i++) {
		std::string queDeviceName = deviceName;

		queDeviceName = deviceName + "@"
				+ boost::lexical_cast<std::string>((int) i);
		/*
		 * http://www.ntop.org/pfring_api/pfring_8h.html#a397061c37a91876b6b68584e2cb99da5
		 */
		pfring_set_poll_watermark(rings[i], 128);

		queueRings_[i] = new ntop::PFring(rings[i], (char*) queDeviceName.data(), snaplen,
				flags);

		if (queueRings_[i]->enable_ring() >= 0) {
			LOG(INFO)<< "Successfully opened device "
			<< queueRings_[i]->get_device_name();
		} else {
			LOG(ERROR) << "Unable to open device " << queDeviceName
			<< "! Is pf_ring not loaded or do you use quick mode and have already a socket bound to this device?!";
			exit(1);
		}
	}

	/*
	 * Start gratuitous ARP request sending thread
	 */
	startThread("ArpSender");
}

void PFringHandler::thread() {

	struct DataContainer arp = EthernetUtils::GenerateGratuitousARPv4(
			GetMyMac().data(), GetMyIP());
	/*
	 * Periodically send a gratuitous ARP frames
	 */
	while (true) {
		SendFrame(arp.data, arp.length);
		boost::this_thread::sleep(boost::posix_time::seconds(60));
	}
}

void PFringHandler::PrintStats() {
	pfring_stat stats = { 0 };
	LOG(INFO)<< "Ring\trecv\tdrop";
	for (int i = 0; i < numberOfQueues_; i++) {
		queueRings_[i]->get_stats(&stats);
		LOG(INFO)<<i << " \t" << stats.recv << "\t" << stats.drop;
	}
}
}
/* namespace na62 */
