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
#include "options/Logging.h"

#include "exceptions/NA62Error.h"

namespace na62 {
namespace dim {

DimListener::DimListener() :
		nextBurstNumber_("RunControl/NextBurstNumber", -1, this), burstNumber_(
				"RunControl/BurstNumber", -1, this), runNumber_(
				"RunControl/RunNumber", -1, this), SOB_TS_("NA62/Timing/SOB", 0,
				this), EOB_TS_("NA62/Timing/EOB", 0, this),
				burstTimeInfo_("RunControl/BurstTimeStruct", nullptr, 0, this), thread(nullptr) {
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
	} else if (curr == &burstTimeInfo_) {
		//LOG_INFO("Received burst time info update");
		if (burstTimeInfo_.getSize() != 0) {
			BurstTimeInfo bti;
			int32_t* rawbti = (int32_t*) burstTimeInfo_.getData();
			bti.burstID = rawbti[0];
			bti.sobTime = rawbti[1];
			bti.eobTime = rawbti[2];
			bti.runNumber = rawbti[3];
			LOG_INFO ("Received burst time info update: " << bti.burstID << " " << bti.sobTime
					<< " " <<bti.eobTime << " " << bti.runNumber);

			if (bti.eobTime != 0 && bti.runNumber != 0) {
				for (auto callback : burstTimeInfoCallbacks) {
					callback(bti);
				}
			}
		}
	}
}

} /* namespace dim */
} /* namespace na62 */
