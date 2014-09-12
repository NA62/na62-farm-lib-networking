/*
 * FarmStarter.h
 *
 *  Created on: Sep 12, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#ifndef DIMLISTENER_H_
#define DIMLISTENER_H_

#include <dim/dic.hxx>
#include <sys/types.h>
#include <algorithm>
#include <vector>
#include <functional>


namespace na62 {
namespace dim {

class DimListener: public DimClient {
public:
	DimListener();
	virtual ~DimListener();

	uint getEobTimeStamp();
	uint getSobTimeStamp();
	uint getRunNumber();
	uint getBurstNumber();

	void registerEobListener(std::function<void(uint)> callback) {
		eobCallbacks.push_back(std::move(callback));
	}
	void registerSobListener(std::function<void(uint)> callback) {
		sobCallbacks.push_back(std::move(callback));
	}
	void registerRunNumberListener(std::function<void(uint)> callback) {
		runNumberCallbacks.push_back(std::move(callback));
	}
	void registerBurstNumberListener(std::function<void(uint)> callback) {
		burstNumberCallbacks.push_back(std::move(callback));
	}

private:
	void infoHandler();

	DimInfo burstNumber_;
	DimInfo runNumber_;
	DimInfo SOB_TS_;
	DimInfo EOB_TS_;

	std::vector<std::function<void(uint)>> eobCallbacks;
	std::vector<std::function<void(uint)>> sobCallbacks;
	std::vector<std::function<void(uint)>> runNumberCallbacks;
	std::vector<std::function<void(uint)>> burstNumberCallbacks;
};
} /* namespace dim */
} /* namespace na62 */
#endif /* DIMLISTENER_H_ */
