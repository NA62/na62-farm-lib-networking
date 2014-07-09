/*
 * MRP.h
 *
 *  Created on: Feb 24, 2012
 *      Author: Jonas Kunze (kunze.jonas@gmail.com)
 */

#pragma once

#ifndef MRP_H_
#define MRP_H_

#include <netinet/in.h>
#include <stdint.h>

#include "../structs/Network.h"

#define __USE_PFRING_FOR_MRP
#define __USE_BIG_ENDIAN_FOR_MRP

namespace na62 {
namespace cream {

#ifdef __USE_BIG_ENDIAN_FOR_MRP
/**
 * Defines the structure of a L1 trigger element as defined in table 4 in NA62-11-02.
 */
struct TRIGGER_RAW_HDR {
	uint32_t timestamp;

	uint8_t fineTime;
	uint8_t requestZeroSuppressed; // lowest bit 1 to request zero suppressed data
	uint16_t triggerTypeWord;

	uint8_t reserved2;
	uint32_t eventNumber :24;
}__attribute__ ((__packed__));

/**
 * Defines the structure of a L1 MRP header as defined in table 4 in NA62-11-02.
 */
struct MRP_RAW_HDR {
	uint16_t numberOfTriggers;
	uint16_t MRPLength;

	uint32_t ipAddress;

	uint32_t reserved;

	void SetNumberOfTriggers(uint16_t triggerNum) {
		numberOfTriggers = htons(triggerNum);

		/*
		 * The length of the MRP packet equals the length of the udp payload. So this is 8 (the length of this header)
		 *  plus the size of a Trigger header times the number of triggers
		 */
		const uint16_t length = sizeof(MRP_RAW_HDR)
				+ triggerNum * sizeof(struct TRIGGER_RAW_HDR);
		MRPLength = htons(length);
	}

	uint16_t getSize(){
		return ntohs(MRPLength);
	}
}__attribute__ ((__packed__));

struct MRP_FRAME_HDR {

	struct UDP_HDR udp;

	struct MRP_RAW_HDR MRP_HDR;

	void SetNumberOfTriggers(uint16_t triggerNum) {
		MRP_HDR.SetNumberOfTriggers(triggerNum);
		/*
		 * The length of the MRP packet equals the length of the udp payload. So this is 8 (the length of this header)
		 *  plus the size of a Trigger header times the number of triggers
		 */
		const uint16_t length = sizeof(struct MRP_RAW_HDR)
				+ triggerNum * sizeof(struct TRIGGER_RAW_HDR);
		udp.setPayloadSize(length);
	}
}__attribute__ ((__packed__));

#else // #ifdef __USE_BIG_ENDIAN_FOR_MRP
/**
 * Defines the structure of a L1 trigger element as defined in table 4 in NA62-11-02.
 */
struct TRIGGER_RAW_HDR {
	uint32_t timestamp;
	uint16_t triggerTypeWord;
	uint8_t requestZeroSuppressed; // lowest bit 1 to request zero suppressed data
	uint8_t fineTime;
	uint32_t eventNumber :24;
	uint8_t reserved2;
}__attribute__ ((__packed__));

/**
 * Defines the structure of a L1 MRP header as defined in table 4 in NA62-11-02.
 */
struct MRP_RAW_HDR {
	uint16_t MRPLength;
	uint16_t numberOfTriggers;
	uint32_t ipAddress;
	uint32_t reserved;

	void SetNumberOfTriggers(uint16_t triggerNum) {
		numberOfTriggers = triggerNum;

		/*
		 * The length of the MRP packet equals the length of the udp payload. So this is 8 (the length of this header)
		 *  plus the size of a Trigger header times the number of triggers
		 */
		const uint16_t length = sizeof(MRP_RAW_HDR) + triggerNum * sizeof(struct TRIGGER_RAW_HDR);
		MRPLength = length;
	}
}__attribute__ ((__packed__));

struct MRP_FRAME_HDR {

#ifdef __USE_PFRING_FOR_MRP
	struct UDP_HDR udp;
#endif

	struct MRP_RAW_HDR MRP_HDR;

	void SetNumberOfTriggers(uint16_t triggerNum) {
		MRP_HDR.SetNumberOfTriggers(triggerNum);
#ifdef __USE_PFRING_FOR_MRP
		/*
		 * The length of the MRP packet equals the length of the udp payload. So this is 8 (the length of this header)
		 *  plus the size of a Trigger header times the number of triggers
		 */
		const uint16_t length = 8 + triggerNum * sizeof(struct TRIGGER_RAW_HDR);
		udp.setPayloadSize(length);
#endif
	}
}__attribute__ ((__packed__));
#endif

}
/* namespace cream */
} /* namespace na62 */
#endif /* MRP_H_ */
