/*
 * DimListener.cpp
 *
 *  Created on: Sep 12, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "DimListener.h"

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <sys/wait.h>
#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>

#include "exceptions/NA62Error.h"

namespace na62 {
namespace dim {

DimListener::DimListener() :
		burstNumber_("RunControl/BurstNumber", -1, this), runNumber_(
				"RunControl/RunNumber", -1, this), SOB_TS_("NA62/Timing/SOB", 0,
				this), EOB_TS_("NA62/Timing/EOB", 0, this) {

//	int runNumber = 0;
//	if (runNumber_.getSize() <= 0) {
//		throw NA62Error(
//				"Unable to connect to RunNumber service! Refusing to start.");
//	}
}

DimListener::~DimListener() {
}

uint DimListener::getEobTimeStamp() {
	return EOB_TS_.getInt();
}

uint DimListener::getSobTimeStamp() {
	return SOB_TS_.getInt();
}

uint DimListener::getRunNumber() {
	return runNumber_.getInt();
}

uint DimListener::getBurstNumber() {
	return burstNumber_.getInt();
}

void DimListener::infoHandler() {
	DimInfo *curr = getInfo();
	if (curr == &EOB_TS_) {
		uint eob = EOB_TS_.getInt();
		for (auto callback : eobCallbacks) {
			callback(eob);
		}
		std::cout << "Updating EOB timestamp to " << eob << std::endl;
	} else if (curr == &SOB_TS_) {
		uint sob = SOB_TS_.getInt();
		for (auto callback : sobCallbacks) {
			callback(sob);
		}
		std::cout << "Updating SOB timestamp to " << sob << std::endl;
	} else if (curr == &runNumber_) {
		uint runNumber = runNumber_.getInt();
		for (auto callback : runNumberCallbacks) {
			callback(runNumber);
		}
		std::cout << "Updating RunNumber to " << runNumber << std::endl;
	} else if (curr == &burstNumber_) {
		uint burstID = burstNumber_.getInt();
		for (auto callback : burstNumberCallbacks) {
			callback(burstID);
		}
		std::cout << "Updating burstID to " << burstID << std::endl;
	}
}

} /* namespace dim */
} /* namespace na62 */
