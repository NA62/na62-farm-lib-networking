/*
 * IPCHandler.cpp
 *
 *  Created on: Nov 26, 2012
 *      Author: Jonas Kunze (kunzej@cern.ch)
 */

#include "IPCHandler.h"

#include <stddef.h>

#include <unistd.h>
#include <iostream>
#include <zmq.h>
#include <zmq.hpp>

#include "../socket/ZMQHandler.h"

/*
 * IPC seems not to be compatible with dim -> use tcp on localhost
 */
#define StateAddress "tcp://127.0.0.1:4500"
#define StatisticsAddress "tcp://127.0.0.1:4501"
#define CommandAddress "tcp://127.0.0.1:4502"

#define StateAddressServer "tcp://*:4500"
#define StatisticsAddressServer "tcp://*:4501"
#define CommandAddressServer "tcp://*:4502"

//#define StateAddress "ipc:///tmp/na62-farm-state"
//#define StatisticsAddress "ipc:///tmp/na62-farm-statistics"
//#define CommandAddress "ipc:///tmp/na62-farm-command"
namespace na62 {

zmq::socket_t* IPCHandler::stateSender_ = nullptr;
zmq::socket_t* IPCHandler::statisticsSender_ = nullptr;
zmq::socket_t* IPCHandler::commandSender_ = nullptr;

zmq::socket_t* IPCHandler::stateReceiver_ = nullptr;
zmq::socket_t* IPCHandler::statisticsReceiver_ = nullptr;
zmq::socket_t* IPCHandler::commandReceiver_ = nullptr;

bool IPCHandler::stateSenderActive_;
bool IPCHandler::statisticsSenderActive_;
bool IPCHandler::commandSenderActive_;

bool IPCHandler::stateReceiverActive_;
bool IPCHandler::statisticsReceiverActive_;
bool IPCHandler::commandReceiverActive_;

void IPCHandler::shutDown() {
	/*
	 * Destroy all sockets. If they are null ZMQHandler will irgnore them
	 */
	if (!stateSenderActive_) {
		ZMQHandler::DestroySocket(stateSender_);
	}
	if (!statisticsSenderActive_) {
		ZMQHandler::DestroySocket(statisticsSender_);
	}
	if (!commandSenderActive_) {
		ZMQHandler::DestroySocket(commandSender_);
	}
	if (!stateReceiverActive_) {
		ZMQHandler::DestroySocket(stateReceiver_);
	}
	if (!statisticsReceiverActive_) {
		ZMQHandler::DestroySocket(statisticsReceiver_);
	}
	if (!commandReceiverActive_) {
		ZMQHandler::DestroySocket(commandReceiver_);
	}

}

bool IPCHandler::isRunning() {
	return ZMQHandler::IsRunning();
}

bool IPCHandler::connectClient() {
	if (!ZMQHandler::IsRunning()) {
		return false;
	}

	stateSender_ = ZMQHandler::GenerateSocket("stateSender_", ZMQ_PUSH);
	statisticsSender_ = ZMQHandler::GenerateSocket("statisticsSender_", ZMQ_PUSH);
	commandReceiver_ = ZMQHandler::GenerateSocket("commandReceiver_", ZMQ_PULL);

	stateSender_->connect(StateAddress);
	statisticsSender_->connect(StatisticsAddress);
	commandReceiver_->bind(CommandAddressServer);

	return true;
}

bool IPCHandler::bindServer() {
	if (!ZMQHandler::IsRunning()) {
		return false;
	}

	stateReceiver_ = ZMQHandler::GenerateSocket("stateReceiver_", ZMQ_PULL);
	statisticsReceiver_ = ZMQHandler::GenerateSocket("statisticsReceiver_", ZMQ_PULL);
	commandSender_ = ZMQHandler::GenerateSocket("commandSender_", ZMQ_PUSH);

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
	if (!ZMQHandler::IsRunning()) {
		return;
	}

	if (!stateSender_ && !connectClient()) {
		return;
	}
	std::cout<<"Sending ZMQ message" << std::endl;
	stateSender_->send((const void*) &newState, (size_t) sizeof(STATE));
	std::cout<<"Sent ZMQ message" <<  std::endl;
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
		zmq::message_t m(message.size());
		memcpy(m.data(), message.data(), message.size());
		statisticsSender_->send(m);
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
			commandSender_ = nullptr;
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
		commandReceiverActive_ = true;
		commandReceiver_->recv(&msg);
		commandReceiverActive_ = false;
		return std::string((const char*) msg.data(), msg.size());
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(commandReceiver_);
			commandReceiver_ = nullptr;
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
		statisticsReceiverActive_ = true;
		if (statisticsReceiver_->recv(&msg)) {
			return std::string((const char*) msg.data(), msg.size());
		}
		statisticsReceiverActive_ = false;
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(statisticsReceiver_);
			statisticsReceiver_ = nullptr;
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
		stateReceiverActive_ = true;
		if (stateReceiver_->recv(&msg)) {
			return *((STATE*) msg.data());
		}
		stateReceiverActive_ = false;
	} catch (const zmq::error_t& ex) {
		if (ex.num() != EINTR) { // try again if EINTR (signal caught)
			ZMQHandler::DestroySocket(stateReceiver_);
			stateReceiver_ = nullptr;
		}
	}
	return TIMEOUT;
}

}
/* namespace na62 */
