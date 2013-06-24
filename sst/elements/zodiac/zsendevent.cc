
#include "zsendevent.h"

using namespace SST::Hermes;
using namespace SST::Zodiac;
using namespace SST;

ZodiacSendEvent::ZodiacSendEvent(uint32_t dest, uint32_t length,
                        PayloadDataType dataType,
                        uint32_t tag, HermesCommunicator group) {

	msgDest = dest;
	msgLength = length;
	msgTag = tag;
	msgType = dataType;
	msgComm = group;
}

ZodiacEventType ZodiacSendEvent::getEventType() {
	return SEND;
}

uint32_t ZodiacSendEvent::getDestination() {
	return msgDest;
}

uint32_t ZodiacSendEvent::getLength() {
	return msgLength;
}

uint32_t ZodiacSendEvent::getMessageTag() {
	return msgTag;
}

PayloadDataType ZodiacSendEvent::getDataType() {
	return msgType;
}

HermesCommunicator ZodiacSendEvent::getCommunicatorGroup() {
	return msgComm;
}
