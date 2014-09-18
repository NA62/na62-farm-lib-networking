/*
 * IPCHandler.cpp
 *
 *  Created on: Nov 26, 2012
 *      Author: Jonas Kunze (kunzej@cern.ch)
 */

#include "IPCHandler.h"

#include <stddef.h>

#ifdef USE_GLOG
#include <glog/logging.h>
#endif
#include <unistd.h>
#include <iostream>
#include <zmq.h>
#include <zmq.hpp>

#include "../socket/ZMQHandler.h"

/*
 * IPC seems not to be compatible with dim -> use tcp on localhost
 */
#define StateAddress "tcp://172.0.0.1:4500"
#define StatisticsAddress "tcp://172.0.0.1:4501"
#define CommandAddress "tcp://172.0.0.1:4502"

#define StateAddressServer "tcp://*:4500"
#define StatisticsAddressServer "tcp://*:4501"
#define CommandAddressServer "tcp://*:4502"

//#define StateAddress "ipc:///tmp/na62-farm-state"
//#define StatisticsAddress "ipc:///tmp/na62-farm-statistics"
//#define CommandAddress "ipc:///tmp/na62-farm-command"
namespace na62 {

STATE IPCHandler::currentState = OFF;
zmq::socket_t* IPCHandler::stateSender_ = nullptr;
zmq::socket_t* IPCHandler::statisticsSender_ = nullptr;
zmq::socket_t* IPCHandler::commandSender_ = nullptr;

zmq::socket_t* IPCHandler::stateReceiver_ = nullptr;
zmq::socket_t* IPCHandler::statisticsReceiver_ = nullptr;
zmq::socket_t* IPCHandler::commandReceiver_ = nullptr;

void IPCHandler::shutDown() {
	/*
	 * Destroy all sockets. If they are null ZMQHandler will irgnore them
	 */
	ZMQHandler::DestroySocket(stateSender_);
	ZMQHandler::DestroySocket(statisticsSender_);
	ZMQHandler::DestroySocket(commandSender_);
	ZMQHandler::DestroySocket(stateReceiver_);
	ZMQHandler::DestroySocket(statisticsReceiver_);
	ZMQHandler::DestroySocket(commandReceiver_);
}

bool IPCHandler::connectClient() {
	if (!ZMQHandler::IsRunning()) {
		return false;
	}

	stateSender_ = ZMQHandler::GenerateSocket(ZMQ_PUSH);
	statisticsSender_ = ZMQHandler::GenerateSocket(ZMQ_PUSH);
	commandReceiver_ = ZMQHandler::GenerateSocket(ZMQ_PULL);

	stateSender_->connect(StateAddress);
	statisticsSender_->connect(StatisticsAddress);
	commandReceiver_->bind(CommandAddressServer);

	return true;
}

bool IPCHandler::bindServer() {
	if (!ZMQHandler::IsRunning()) {
		return false;
	}

	statisticsReceiver_ = ZMQHandler::GenerateSocket(ZMQ_PULL);
	stateReceiver_ = ZMQHandler::GenerateSocket(ZMQ_PULL);
	commandSender_ = ZMQHandler::GenerateSocket(ZMQ_PUSH);

	stateReceiver_->bind(StateAddressServer);
	statisticsReceiver_->bind(StatisticsAddressServer);
	commandSender_->connect(CommandAddress);

	return true;
}

/**
 * Sets the receive timeout of the statistics and state receiver sockets
 */
void IPCHandler::setTimeout(int timeout) {
	if (!statisticsReceiver_ && !bindServer()) {
		return;
	}

	statisticsReceiver_->setsockopt(ZMQ_RCVTIMEO, (const void*) &timeout,
			(size_t) sizeof(timeout));
	stateReceiver_->setsockopt(ZMQ_RCVTIMEO, (const void*) &timeout,
			(size_t) sizeof(timeout));
}

void IPCHandler::updateState(STATE newState) {
	currentState = newState;
	if (!ZMQHandler::IsRunning()) {
		return;
	}

	if (!stateSender_ && !connectClient()) {
		return;
	}

	stateSender_->send((const void*) &currentState, (size_t) sizeof(STATE));
}

void IPCHandler::sendErrorMessage(std::string message) {
	sendStatistics("ErrorMessage", message);
}

void IPCHandler::sendStatistics(std::string name, std::string values) {
	if (!ZMQHandler::IsRunning()) {
		return;
	}

	if (!statisticsSender_ && !connectClient()) {
		return;
	}

	if (!statisticsSender_ || name.empty() || values.empty()) {
		return;
	}

	std::string message = name + ":" + values;

	try {
		statisticsSender_->send((const void*) message.data(),
				(size_t) message.length());
	} catch (const zmq::error_t& ex) {
	}
}

/**
 * Sends the given string to the remote process calling {@link getNextCommand}
 */
void IPCHandler::sendCommand(std::string command) {
	if (!ZMQHandler::IsRunning()) {
		return;
	}

	if (!commandSender_ && !bindServer()) {
		return;
	}

	if (!commandSender_ || command.empty()) {
		return;
	}
	try {
		if (commandSender_->connected()) {
			commandSender_->send((const void*) command.data(),
					(size_t) command.length());
		}
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(commandSender_);
		}
	}
}

/**
 * Blocks until the next command has been received
 */
std::string IPCHandler::getNextCommand() {
	if (!ZMQHandler::IsRunning()) {
		return "";
	}

	if (!commandReceiver_ && !connectClient()) {
		return "";
	}

	zmq::message_t msg;
	try {
		commandReceiver_->recv(&msg);
		return std::string((const char*) msg.data(), msg.size());
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(commandReceiver_);
		}
	}
	return "";
}

std::string IPCHandler::tryToReceiveStatistics() {
	if (!ZMQHandler::IsRunning()) {
		return "";
	}

	if (!statisticsReceiver_ && !bindServer()) {
		return "";
	}

	zmq::message_t msg;

	try {
		if (statisticsReceiver_->recv(&msg)) {
			return std::string((const char*) msg.data(), msg.size());
		}
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(statisticsReceiver_);
		}
	}
	return "";
}

STATE IPCHandler::tryToReceiveState() {
	if (!ZMQHandler::IsRunning()) {
		return TIMEOUT;
	}

	if (!stateReceiver_ && !bindServer()) {
		return TIMEOUT;
	}

	zmq::message_t msg;
	try {
		if (stateReceiver_->recv(&msg)) {
			return *((STATE*) msg.data());
		}
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(stateReceiver_);
		}
	}
	return TIMEOUT;
}

}
/* namespace na62 */
