/*
 * ZMQHandler.cpp
 *
 *  Created on: Feb 21, 2014
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "ZMQHandler.h"

#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/date_time/time_duration.hpp>
#include <boost/thread/thread.hpp>
#include <glog/logging.h>
#include <zmq.h>
#include <zmq.hpp>
#include <iostream>
#include <map>
#include <mutex>

namespace na62 {

zmq::context_t* ZMQHandler::context_;
bool ZMQHandler::running_ = true;
std::atomic<int> ZMQHandler::numberOfActiveSockets_;
std::set<std::string> ZMQHandler::boundAddresses_;
std::mutex ZMQHandler::connectMutex_;

void ZMQHandler::Initialize(const int numberOfIOThreads) {
	context_ = new zmq::context_t(numberOfIOThreads);
}
void ZMQHandler::Stop() {
	running_ = false;
}

void ZMQHandler::shutdown() {
	while (numberOfActiveSockets_ != 0) {
		usleep(1000);
	}

	delete context_;
}

zmq::socket_t* ZMQHandler::GenerateSocket(int socketType, int highWaterMark) {
	int linger = 0;
	zmq::socket_t* socket = new zmq::socket_t(*context_, socketType);
	socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger));

	socket->setsockopt(ZMQ_SNDHWM, &highWaterMark, sizeof(highWaterMark));

	numberOfActiveSockets_++;
	return socket;
}

void ZMQHandler::DestroySocket(zmq::socket_t* socket) {
	if (socket == nullptr) {
		return;
	}

	socket->close();
	delete socket;
	numberOfActiveSockets_--;
	std::cout << "Closed ZMQ socket (" << numberOfActiveSockets_
			<< " remaining)" << std::endl;
}

std::string ZMQHandler::GetEBL0Address(int threadNum) {
	std::stringstream address;
	address << "inproc://EB/" << threadNum << "/L0";
	return address.str();
}

std::string ZMQHandler::GetEBLKrAddress(int threadNum) {
	std::stringstream address;
	address << "inproc://EB/" << threadNum << "/LKr";
	return address.str();
}

void ZMQHandler::BindInproc(zmq::socket_t* socket, std::string address) {
	socket->bind(address.c_str());

	std::lock_guard<std::mutex> lock(connectMutex_);
	boundAddresses_.insert(address);
}

void ZMQHandler::ConnectInproc(zmq::socket_t* socket, std::string address) {
	connectMutex_.lock();
	while (boundAddresses_.find(address) == boundAddresses_.end()) {
		connectMutex_.unlock();
		LOG(INFO)<< "ZMQ not yet bound: " << address;
		boost::this_thread::sleep(boost::posix_time::microsec(500000));
		connectMutex_.lock();
	}
	socket->connect(address.c_str());
	connectMutex_.unlock();
}

void ZMQHandler::sendMessage(zmq::socket_t* socket,
		zmq::message_t&& msg, int flags) {
	while (ZMQHandler::IsRunning()) {
		try {
			socket->send(msg, flags);
			break;
		} catch (const zmq::error_t& ex) {
			if (ex.num() != EINTR) { // try again if EINTR (signal caught)
				LOG(ERROR)<<ex.what();

				ZMQHandler::DestroySocket (socket);
				return;
			}
		}
	}
}

}
/* namespace na62 */
