/*
 * VanillaMrpSender.h
 *
 *  Created on: Sep 25, 2014
 *      Author: root
 */

#ifndef VANILLAMRPSENDER_H_
#define VANILLAMRPSENDER_H_

#include <boost/asio.hpp>
#include <sys/types.h>
#include <string>

namespace na62 {
namespace cream {

class VanillaMrpSender {
public:
	VanillaMrpSender(const std::string multicastAddr, const ushort dstPort) :
			socket(service) {

		boost::asio::ip::address address =
				boost::asio::ip::address::from_string(multicastAddr.c_str());

		// Create socket.
		using boost::asio::ip::udp;

		socket.open(boost::asio::ip::udp::v4());

		/*
		 * Set TTL
		 */
		boost::asio::ip::multicast::hops option(10);
		socket.set_option(option);

		dstEndpoint_ = new udp::endpoint(address, dstPort);
	}

	~VanillaMrpSender() {
	}

public:
	void send_data(const char* data, const uint dataLength) {
		socket.send_to(boost::asio::buffer(data, dataLength), *dstEndpoint_);
	}

private:
	boost::asio::io_service service;
	boost::asio::ip::udp::socket socket;
	boost::asio::ip::udp::endpoint* dstEndpoint_;
};

} /* namespace cream */
} /* namespace na62 */

#endif /* VANILLAMRPSENDER_H_ */
