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

#include <options/Logging.h>

namespace na62 {

zmq::context_t* ZMQHandler::context_;
bool ZMQHandler::running_ = true;
std::atomic<int> ZMQHandler::numberOfActiveSockets_;
std::set<std::string> ZMQHandler::boundAddresses_;
std::mutex ZMQHandler::connectMutex_;
std::map<zmq::socket_t*, std::string> ZMQHandler::allSockets_;

void ZMQHandler::Initialize(const int numberOfIOThreads) {
	context_ = new zmq::context_t(numberOfIOThreads); // can throw!
}
void ZMQHandler::Stop() {
	running_ = false;
}

void ZMQHandler::shutdown() {
	delete context_;
}

zmq::socket_t* ZMQHandler::GenerateSocket(std::string name, int socketType,
		int highWaterMark) {
	int linger = 0;
	zmq::socket_t* socket = new zmq::socket_t(*context_, socketType); // can throw!
	socket->setsockopt(ZMQ_LINGER, &linger, sizeof(linger)); // can throw!

	socket->setsockopt(ZMQ_SNDHWM, &highWaterMark, sizeof(highWaterMark)); // can throw!

	numberOfActiveSockets_++;
	allSockets_[socket] = name;
	return socket;
}

void ZMQHandler::DestroySocket(zmq::socket_t* socket) {
	if (socket == nullptr) {
		return;
	}

	allSockets_.erase(socket);
	std::string missingSockets;
	for (auto socketAndName : allSockets_) {
		missingSockets += socketAndName.second + ",";
	}

	socket->close();
	delete socket;
	numberOfActiveSockets_--;
	LOG_INFO("Closed ZMQ socket (" << numberOfActiveSockets_
			<< " remaining: " << missingSockets << ")");
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
		LOG_INFO("ZMQ not yet bound: " << address);
		boost::this_thread::sleep(boost::posix_time::microsec(500000));
		connectMutex_.lock();
	}
	socket->connect(address.c_str());
	connectMutex_.unlock();
}

void ZMQHandler::sendMessage(zmq::socket_t* socket, zmq::message_t&& msg,
		int flags) {
	while (ZMQHandler::IsRunning()) {
		try {
			socket->send(msg, flags);
			break;
		} catch (const zmq::error_t& ex) {
			if (ex.num() != EINTR) { // try again if EINTR (signal caught)
				LOG_ERROR("Failed to send message over ZMQ because " << ex.what());
				ZMQHandler::DestroySocket(socket);
				return;
			}
		}
	}
}

}
/* namespace na62 */
