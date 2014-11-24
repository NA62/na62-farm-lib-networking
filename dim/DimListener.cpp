/*
 * DimListener.cpp
 *
 *  Created on: Sep 12, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#include "DimListener.h"

#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
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
		nextBurstNumber_("RunControl/NextBurstNumber", -1, this), burstNumber_(
				"RunControl/BurstNumber", -1, this), runNumber_(
				"RunControl/RunNumber", -1, this), SOB_TS_("NA62/Timing/SOB", 0,
				this), EOB_TS_("NA62/Timing/EOB", 0, this), runningMerger_(
				"RunControl/RunningMergers", (char*) "", this), thread(nullptr) {

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

uint DimListener::getNextBurstNumber() {
	return nextBurstNumber_.getInt();
}

std::string DimListener::getRunningMergers() {
	return std::string((char*) runningMerger_.getData(),
			runningMerger_.getSize());
}

void DimListener::infoHandler() {
	DimInfo *curr = getInfo();
	if (curr == &EOB_TS_) {
		uint eob = EOB_TS_.getInt();
		for (auto callback : eobCallbacks) {
			callback(eob);
		}
	} else if (curr == &SOB_TS_) {
		uint sob = SOB_TS_.getInt();
		for (auto callback : sobCallbacks) {
			callback(sob);
		}
	} else if (curr == &runNumber_) {
		uint runNumber = runNumber_.getInt();
		for (auto callback : runNumberCallbacks) {
			callback(runNumber);
		}
	} else if (curr == &burstNumber_) {
		uint burstID = burstNumber_.getInt();
		for (auto callback : burstNumberCallbacks) {
			callback(burstID);
		}
	} else if (curr == &nextBurstNumber_) {
		uint burstID = nextBurstNumber_.getInt();
		for (auto callback : nextBurstNumberCallbacks) {
			callback(burstID);
		}
	} else if (curr == &runningMerger_) {
		std::string runningMergerList((char*) runningMerger_.getData(),
				runningMerger_.getSize());
		boost::trim(runningMergerList); // trim the string to remove any outer whitespace
		if (!runningMergerList.empty()) {
			for (auto callback : runningMergerCallbacks) {
				callback(runningMergerList);
			}
		}
	}
}

} /* namespace dim */
} /* namespace na62 */
