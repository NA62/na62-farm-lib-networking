/*
 * IPCHandler.h
 *
 *  Created on: Nov 26, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once
#ifndef IPCHANDLER_H_
#define IPCHANDLER_H_

#include <string>
#include <functional>

namespace zmq {
class socket_t;
} /* namespace zmq */

namespace na62 {

enum STATE {
	// 0=IDLE; 1=INITIALIZED; 2=RUNNING; Other=ERROR, TIMOUE=not to be sent
	OFF,
	INITIALIZING,
	RUNNING,
	ERROR,
	TIMEOUT
};

class IPCHandler {
public:

	static void updateState(STATE newState);
	static void sendErrorMessage(std::string Message);
	static void sendStatistics(std::string name, std::string values);
	static void sendCommand(std::string command);

	static std::string tryToReceiveStatistics();
	static STATE tryToReceiveState();
	static std::string getNextCommand();

	static void setTimeout(int timeout);

	static void shutDown();
private:
	static void connectClient();

	static void bindServer();

	static STATE currentState;

	static zmq::socket_t* stateSender_;
	static zmq::socket_t* statisticsSender_;
	static zmq::socket_t* commandSender_;

	static zmq::socket_t* stateReceiver_;
	static zmq::socket_t* statisticsReceiver_;
	static zmq::socket_t* commandReceiver_;
};

} /* namespace na62 */
#endif /* IPCHANDLER_H_ */
