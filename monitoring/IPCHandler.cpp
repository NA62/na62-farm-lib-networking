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

#define StateAddress "ipc:///tmp/na62-farm-state"
#define StatisticsAddress "ipc:///tmp/na62-farm-statistics"
#define CommandAddress "ipc:///tmp/na62-farm-command"

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

void IPCHandler::connectClient() {
	stateSender_ = ZMQHandler::GenerateSocket(ZMQ_PUSH);
	statisticsSender_ = ZMQHandler::GenerateSocket(ZMQ_PUSH);
	commandReceiver_ = ZMQHandler::GenerateSocket(ZMQ_PULL);

	commandReceiver_->connect(CommandAddress);
	stateSender_->connect(StateAddress);
	statisticsSender_->connect(StatisticsAddress);
}

void IPCHandler::bindServer() {
	statisticsReceiver_ = ZMQHandler::GenerateSocket(ZMQ_PULL);
	stateReceiver_ = ZMQHandler::GenerateSocket(ZMQ_PULL);
	commandSender_ = ZMQHandler::GenerateSocket(ZMQ_PUSH);

	stateReceiver_->bind(StateAddress);
	statisticsReceiver_->bind(StatisticsAddress);
	commandSender_->bind(CommandAddress);
}

/**
 * Sets the receive timeout of the statistics and state receiver sockets
 */
void IPCHandler::setTimeout(int timeout) {
	if (!statisticsReceiver_) {
		bindServer();
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
	if (!stateSender_) {
		connectClient();
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
	if (!statisticsSender_) {
		connectClient();
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
	if (!commandSender_) {
		bindServer();
	}

	if (!commandSender_ || command.empty()) {
		return;
	}

	commandSender_->send((const void*) command.data(),
			(size_t) command.length());
}

/**
 * Blocks until the next command has been received
 */
std::string IPCHandler::getNextCommand() {
	if (!ZMQHandler::IsRunning()) {
		return "";
	}
	if (!commandReceiver_) {
		connectClient();
	}

	zmq::message_t msg;
	commandReceiver_->recv(&msg);
	return std::string((const char*) msg.data(), msg.size());
	return "";
}

std::string IPCHandler::tryToReceiveStatistics() {
	if (!ZMQHandler::IsRunning()) {
		return "";
	}

	if (!statisticsReceiver_) {
		bindServer();
	}

	zmq::message_t msg;
	if (statisticsReceiver_->recv(&msg)) {
		return std::string((const char*) msg.data(), msg.size());
	}
	return "";
}

STATE IPCHandler::tryToReceiveState() {
	if (!ZMQHandler::IsRunning()) {
		return TIMEOUT;
	}

	if (!stateReceiver_) {
		bindServer();
	}

	zmq::message_t msg;

	if (stateReceiver_->recv(&msg)) {
		return *((STATE*) msg.data());
	}

	return TIMEOUT;
}

}
/* namespace na62 */
