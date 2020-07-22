#ifndef _GNU_SOURCE
    #define _GNU_SOURCE 1 // to be able to use asprintf()
#endif

#include <stdio.h>
#include <assert.h>
#include "COCOReceiver.h"
#include "PulseRecorder.h"

#if COCODebugLogging
    #define DebugLog(format, ...) printf(format, ## __VA_ARGS__)
#else
    #define DebugLog(format, ...)
#endif

/*
References COCO protocol:  
http://members.home.nl/hilcoklaassen/
https://manual.pilight.org/protocols/433.92/contact/kaku.html
http://mientki.ruhosting.nl/data_www/raspberry/doc/klikaanklikuit.html

Understanding of recognizing *any* protocol was obtained by porting the 
following Python code to C:
https://github.com/milaq/rpi-rf/blob/master/rpi_rf/rpi_rf.py

Some other references that didn't help me really, but still listing them:
https://jeelabs.org/2009/03/10/decoding-433-mhz-kaku-signals/
https://github.com/chaanstra/raspKaku/blob/master/kaku.cpp
https://github.com/mroest/kaku-controller
https://raw.githubusercontent.com/flatsiedatsie/433Switches/master/433cloner.py


element  || start-sync | 26-bit address | 1-bit group | 1-bit on/off | 4-bit channel | stop-sync || TOTAL
‾‾‾‾‾‾‾‾‾||‾‾‾‾‾‾‾‾‾‾‾‾|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|‾‾‾‾‾‾‾‾‾‾‾‾‾|‾‾‾‾‾‾‾‾‾‾‾‾‾‾|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|‾‾‾‾‾‾‾‾‾‾‾||‾‾‾‾‾‾‾‾
periods  ||       2    |       104      |     4       |       4      |       16      |    2      ||  132

t = 260µs LOW
T = 260µs HIGH

> startsync:
 T          10t        
‾‾|                    
  |                    
  |____________________

> stopsync:
T                                        40t           
‾‾|                                                                                
  |                                                                                
  |________________________________________________________________________________

> '0'
T   t  T     4t
‾‾|  |‾‾|
  |  |  |
  |__|  |________

> '1'
T     4t     T  t
‾‾|        |‾‾|
  |        |  |
  |________|  |__
*/


// the protocol actually contains 132 pulses, but this program ignores the 
// first T of the start-sync
const uint32_t COCOMessagePulseCount = 131;

// the number of high or low pulses that encode a zero or a one
const uint32_t COCOPulsesPerBit = 4;

// the length of a long pulse, expressed in number of singlepulseDurations
const uint32_t COCOPulsesLong = 4;

// the length of a short pulse, expressed in number of singlepulseDuration
const uint32_t COCOPulsesShort = 1;

// the length of a long start sync pulse, expressed in number of singlepulseDuration
const uint32_t COCOStartSyncLowPulsesCount = 10;

// the length of a long end sync pulse, expressed in number of singlepulseDuration
const uint32_t COCOEndSyncLowPulsesCount = 40;

struct COCOReceiver
{
    // publicly queryable properties
    uint32_t repeatCount;       // count of repeated detections of a COCOMessage that should trigger a callback
    uint32_t refractoryPeriod;  // seconds
    
    uint32_t positiveTolerance; // percentage, e.g. 40 means 40%
    uint32_t negativeTolerance; // percentage, e.g. 40 means 40%
    uint32_t singlePulseDuration; // µicro seconds


    // for internal use
    //
    COCOMessageDetected callback;
    uint32_t repeats;
    uint32_t lastTimestamp;
    uint32_t *durations;
    uint32_t durationsIndex;
    uint16_t channelMask;
    uint32_t onOffMask;
    uint32_t groupMask;
    uint32_t addressMask;
    uint32_t previousMessageCode;
    uint32_t startTime;
    uint32_t timestampPreviousHit;

    uint32_t singlePulseMaxDuration;
    uint32_t singlePulseMinDuration;
    uint32_t startSyncLowMinDuration;
    uint32_t startSyncLowMaxDuration;
    uint32_t endSyncLowMinDuration;
    uint32_t endSyncLowMaxDuration;

    PulseRecorderRef pulseRecorder;
};

struct COCOMessage
{
    uint32_t timestamp; // timestamp of the end of the long part of the start-sync 

    // all 32 bits encoding the message (i.e. address, group, onOff and channel)
    uint32_t fullMessageCode;

    // only 26 bits in reality, but no such thing as a 26-bit integer type
    uint32_t address; 
    bool group;
    bool onOff;
    uint16_t channel;
};

uint32_t COCOMessageGetAddress(COCOMessageRef message)
{ assert(NULL != message); return message->address; }

bool COCOMessageGetGroup(COCOMessageRef message)
{ assert(NULL != message); return message->group; }

bool COCOMessageGetOnOff(COCOMessageRef message)
{ assert(NULL != message); return message->onOff; }

uint16_t COCOMessageGetChannel(COCOMessageRef message)
{ assert(NULL != message); return message->channel; }



void printBinary(uint32_t value, int size)
{
    for (size_t index = 1; index <= size; index++)
    {
        int shiftValue = size - index;
        uint32_t bitValue = (value >> shiftValue) & 1;
        char bitRepresentation = bitValue ? '1' : '0';
        DebugLog("%c", bitRepresentation);
        fflush(stdout);
    }
}

void binaryRepresentation(uint32_t value, int size, char *buffer)
{
	assert(size <= 32);

    for (size_t index = 0; index < size; index++)
    {
    	uint32_t shiftValue = size - (index + 1);
        uint32_t bitValue = (value >> shiftValue) & 1;

        // note: index 0 will represent the most significant bit
        buffer[index] = bitValue ? '1' : '0';
    }
    buffer[size] = '\0';
}

bool analyzeDurations(COCOReceiverRef receiver, uint32_t timestamp, struct COCOMessage * message)
{
    assert(NULL != receiver);

    // this can only be a valid COCO message if there are 
    // COCOMessagePulseCount pulses
    if (receiver->durationsIndex < COCOMessagePulseCount - 1)
    { return false; }

    uint32_t singlePulseDuration = (receiver->durations[0] + receiver->durations[receiver->durationsIndex]) / 
                        (COCOStartSyncLowPulsesCount + COCOEndSyncLowPulsesCount);

    uint32_t minDuration = 0xffffffff;
    uint32_t maxDuration = 0;

    uint32_t code = 0;
    uint32_t codeLength = 0;
    uint32_t bitValues[COCOPulsesPerBit];
    uint32_t bitIndex = 0;
    for (uint32_t index = 0; index <= receiver->durationsIndex; index++)
    {
        // first duration that is stored is always the start sync
        if (0 == index)
        {
            DebugLog("start:\t%5lu\n", receiver->durations[index]);
            continue;
        }

        // this index marks the end, as does the value, so this is actuall
        // redundant. This could be done with either the index or the value
        if (index == (COCOMessagePulseCount - 1) &&
            receiver->durations[index] > receiver->endSyncLowMinDuration &&
            receiver->durations[index] < receiver->endSyncLowMaxDuration)
        {
            DebugLog("end:\t%5lu\n", receiver->durations[index]);
            break;
        }

        if (receiver->durations[index] < 1000)
        {
            maxDuration = receiver->durations[index] > maxDuration ? receiver->durations[index] : maxDuration;
        }    
            minDuration = receiver->durations[index] < minDuration ? receiver->durations[index] : minDuration;

        // all other durations come in groups of four:
        // a '1' or a '0'
        bitIndex = (index - 1) % COCOPulsesPerBit;
        bitValues[bitIndex] = receiver->durations[index];
        if (bitIndex == (COCOPulsesPerBit - 1))
        {
            DebugLog("[%2lu]\t%5lu %5lu %5lu %5lu", (index / COCOPulsesPerBit) - 1, 
                bitValues[0], 
                bitValues[1], 
                bitValues[2],
                bitValues[3]);

            if (bitValues[0] < receiver->singlePulseMaxDuration && bitValues[0] > receiver->singlePulseMinDuration &&
                bitValues[1] < receiver->singlePulseMaxDuration && bitValues[1] > receiver->singlePulseMinDuration &&
                bitValues[2] < receiver->singlePulseMaxDuration && bitValues[2] > receiver->singlePulseMinDuration &&
                bitValues[3] < (COCOPulsesLong * receiver->singlePulseMaxDuration) && (bitValues[3] >  COCOPulsesLong * receiver->singlePulseMinDuration)
                )
            {
                // '0'
                code <<= 1;
                codeLength += 1;
            }
            else if (bitValues[0] < receiver->singlePulseMaxDuration && bitValues[0] > receiver->singlePulseMinDuration &&
                     bitValues[1] < (COCOPulsesLong * receiver->singlePulseMaxDuration) && (bitValues[1] >  COCOPulsesLong * receiver->singlePulseMinDuration) &&
                     bitValues[2] < receiver->singlePulseMaxDuration && bitValues[2] > receiver->singlePulseMinDuration &&
                     bitValues[3] < receiver->singlePulseMaxDuration && bitValues[3] > receiver->singlePulseMinDuration
                     )
            {
                // '1'
                code <<= 1;
                code |= 1;

                codeLength += 1;
            }
            else
            {
                DebugLog("\nNot a valid bit-encoding.\n");
                return false;
            }

            DebugLog("\t%5llu\n", code & 1);
        }
    }

    DebugLog("code:\t%lu\t", code);
    printBinary(code, codeLength);
    DebugLog("\nCodeLength:\t%2lu\n", codeLength);
    DebugLog("estimated pulse duration:\t%lu\n", singlePulseDuration);
    DebugLog("min pulse duration:\t%lu\n", minDuration);
    DebugLog("min pulse duration:\t%lu\n", maxDuration);

    if (NULL != receiver->pulseRecorder)
    {   
    	char * binary = malloc(sizeof(char) * (codeLength + 1)); // + 1 for terminating NULL
    	binaryRepresentation(code, codeLength, binary);
        char* description;
        int bytesPrinted = asprintf(&description, "\ncode: %s\nlength: %lu\nestimated pulse T: %lu\nmin pulse T: %lu\nmax pulse T: %lu\n", binary, codeLength, singlePulseDuration, minDuration, maxDuration);
        free(binary);
        if (bytesPrinted > 0)
        {
            PulseRecorderAddSequenceDescription(receiver->pulseRecorder, description);
        }
        PulseRecorderAddPulses(receiver->pulseRecorder, receiver->durations, receiver->durationsIndex + 1);

        // 01100101001111111011101110 01 0000
    }

    if (NULL != message)
    {
        message->timestamp = timestamp;
        message->fullMessageCode = code;
        message->address = (code & receiver->addressMask) >> 6;
        message->group = (code & receiver->groupMask) == receiver->groupMask;
        message->onOff = (code & receiver->onOffMask) == receiver->onOffMask;
        message->channel = (uint16_t) (code & receiver->channelMask);
    }

    return true;
}

void COCOReceiverSetRecordReceivedTransmissions(COCOReceiverRef receiver, bool shouldRecord)
{
    // remove existing recorder
    if (NULL != receiver->pulseRecorder)
    {
        PulseRecorderRelease(receiver->pulseRecorder);
        receiver->pulseRecorder = NULL;
    }

    PulseRecorderRef recorder = PulseRecorderCreate("COCOTransmitRecording.txt");
    if (NULL != recorder)
    {
        receiver->pulseRecorder = recorder;
    }
    else 
    {
        printf("COCOReceiverSetRecordReceivedTransmissions: could not create PulseRecorder");
    }
}

COCOMessageRef COCOMessageCreate()
{
    COCOMessageRef message = malloc(sizeof(struct COCOMessage));
    if (NULL != message)
    {
        message->address = 0;
        message->group = false;  
        message->onOff = false;
        message->channel = 0;
    }
    return message;
}

// Actual 'meat' of a COCOReceiver
void COCOReceiverFeedGPIOValueChangeTime(COCOReceiverRef receiver, uint32_t timestamp)
{
    assert(NULL != receiver);

    uint32_t duration = timestamp - receiver->lastTimestamp;
    
    if (duration > receiver->startSyncLowMinDuration && duration < receiver->startSyncLowMaxDuration)
    {
        // start-sync received, start a new sequence
        receiver->startTime = timestamp;
        receiver->durationsIndex = 0;
    }

    if (duration > receiver->endSyncLowMinDuration &&
        duration < receiver->endSyncLowMaxDuration)
    {
        // initialize an empty COCOMessage struct, and hae the analyzeDurations()
        // function populate it if a valid message was found
        COCOMessageRef message =  COCOMessageCreate();

        // ownership is handed over to the callback
        // but the callback is not always called, so in case it is not called
        // the message if freed by us
        bool messageCallback = false;

        // end-sync received, analyze what was received
        receiver->durations[receiver->durationsIndex] = duration;
        if (analyzeDurations(receiver, receiver->startTime, message))
        {
            // if this message was the same one as before,
            // repeats goes +1
            if (message->fullMessageCode == receiver->previousMessageCode)
            {
                receiver->repeats += 1;

                // COCO senders send their message several times
                // if a certain number of repeats is detected, count this as
                // a hit
                if (receiver->repeats >= receiver->repeatCount) 
                {
                    // only count this as a hit, if the previous hit
                    // was more than 3 seconds ago (this program was written for
                    // a doorbell originally, for (dimming) switches, maybe 
                    // 3-seconds spacing is too much. You might want none, and
                    // just increase the repeats instead.
                    if (message->timestamp - receiver->timestampPreviousHit > (receiver->refractoryPeriod * 1000000))
                    {
                        receiver->timestampPreviousHit = message->timestamp;
                        receiver->repeats = 0;
                        DebugLog("timestamp:\t%lu\n", message->timestamp);
                        DebugLog("fullcode:\t%lu", message->fullMessageCode);
                        printBinary(message->fullMessageCode, 32);
                        DebugLog("\n");
                        DebugLog("address:\t%lu\n", message->address);
                        DebugLog("group:\t\t%i\n", message->group);
                        DebugLog("onOff:\t\t%i\n", message->onOff);
                        DebugLog("channel:\t%u\n", message->channel);

                        if (NULL != receiver->callback)
                        {
                            messageCallback = true;
                            receiver->callback(receiver, message);
                        }
                    }
                }
            }
            else 
            {
                receiver->repeats = 0;
            }
            receiver->previousMessageCode = message->fullMessageCode;
        }
        else 
        {
            receiver->previousMessageCode = 0;
        }

        // callback was not called, free the message ourselves
        if (!messageCallback) { COCOMessageRelease(message); }
    }
    
    receiver->durations[receiver->durationsIndex] = duration;
    receiver->durationsIndex += 1;

    // COCO protocol only has 132 durations, and this program does not
    // store the first T of the sync bit, so only 131 needed
    if (receiver->durationsIndex == COCOMessagePulseCount)
    { receiver->durationsIndex = 0; }
    
    receiver->lastTimestamp = timestamp;
}

void updateDurationsForReceiver(COCOReceiverRef receiver)
{
    assert(NULL != receiver);
    receiver->singlePulseMinDuration = receiver->singlePulseDuration * (100 - receiver->negativeTolerance) / 100;
    receiver->singlePulseMaxDuration = receiver->singlePulseDuration * (100 + receiver->positiveTolerance) / 100;

    receiver->startSyncLowMinDuration = receiver->singlePulseDuration * COCOStartSyncLowPulsesCount * (100 - receiver->negativeTolerance) / 100;
    receiver->startSyncLowMaxDuration = receiver->singlePulseDuration * COCOStartSyncLowPulsesCount * (100 + receiver->positiveTolerance) / 100;

    receiver->endSyncLowMinDuration = receiver->singlePulseDuration * COCOEndSyncLowPulsesCount * (100 - receiver->negativeTolerance) / 100;
    receiver->endSyncLowMaxDuration = receiver->singlePulseDuration * COCOEndSyncLowPulsesCount * (100 + receiver->positiveTolerance) / 100;
}

// creation, configuration and querying
COCOReceiverRef COCOReceiverCreate()
{
    COCOReceiverRef newReceiver = malloc(sizeof(struct COCOReceiver));
    if (NULL != newReceiver)
    {
        // set defaults
        // newReceiver->durations is freed in COCOReceiverRelease()
        newReceiver->durations = malloc(sizeof(uint32_t) * COCOMessagePulseCount);
        newReceiver->repeatCount = 1;
        newReceiver->refractoryPeriod = 0;

        newReceiver->positiveTolerance = 40;
        newReceiver->negativeTolerance = 40;
        newReceiver->singlePulseDuration = 260;

        updateDurationsForReceiver(newReceiver);

        newReceiver->repeats = 0;
        newReceiver->lastTimestamp = 0;
        newReceiver->durationsIndex = 0;

        // 26-bit address | 1-bit group | 1-bit on/off | 4-bit channel
        newReceiver->channelMask = 0b00001111;
        newReceiver->onOffMask =   0b00010000;
        newReceiver->groupMask =   0b00100000;
        newReceiver->addressMask = 0b11111111111111111111111111000000;

        newReceiver->previousMessageCode = 0;
        newReceiver->startTime = 0;
        newReceiver->timestampPreviousHit = 0;

        newReceiver->pulseRecorder = NULL;
    }
    return newReceiver;
}


void COCOMessageRelease(COCOMessageRef message)
{
    if (NULL != message) { free(message); }
}

void COCOReceiverRelease(COCOReceiverRef receiver)
{
    if (NULL != receiver) 
    {
        free(receiver->durations);

		if (NULL != receiver->pulseRecorder)
		{
			PulseRecorderRelease(receiver->pulseRecorder);
			receiver->pulseRecorder = NULL;
		}        
		

        free(receiver); 
    }
}

void COCOReceiverSetCallback(COCOReceiverRef receiver, COCOMessageDetected callback)
{
    assert(NULL != receiver);
    receiver->callback = callback;
}
void COCOReceiverSetRepeatCount(COCOReceiverRef receiver, uint32_t repeatCount)
{
    assert(NULL != receiver);
    receiver->repeatCount = repeatCount;
}
void COCOReceiverSetRefractoryPeriod(COCOReceiverRef receiver, uint32_t refractoryPeriod)
{
    assert(NULL != receiver);
    receiver->refractoryPeriod = refractoryPeriod;
}
void COCOReceiverSetPositiveTolerance(COCOReceiverRef receiver, uint32_t tolerance)
{
    assert(NULL != receiver);
    assert(tolerance > 0 && tolerance <= 100);

    uint32_t newValue = newValue > 100 ? 100 : newValue;
    receiver->positiveTolerance = newValue;
    updateDurationsForReceiver(receiver);
}
void COCOReceiverSetNegativeTolerance(COCOReceiverRef receiver, uint32_t tolerance)
{
    assert(NULL != receiver);
    assert(tolerance > 0 && tolerance <= 100);

    uint32_t newValue = newValue > 100 ? 100 : newValue;
    receiver->negativeTolerance = newValue;
    updateDurationsForReceiver(receiver);
}
void COCOReceiverSetSinglePulseDuration(COCOReceiverRef receiver, uint32_t pulseLDuration)
{
    assert(NULL != receiver);
    receiver->singlePulseDuration = pulseLDuration;
    updateDurationsForReceiver(receiver);
}

uint32_t COCOReceiverGetRepeatCount(COCOReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->repeatCount;
}
uint32_t COCOReceiverGetRefractoryPeriod(COCOReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->refractoryPeriod;
}
uint32_t COCOReceiverGetPositiveTolerance(COCOReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->positiveTolerance;
}
uint32_t COCOReceiverGetNegativeTolerance(COCOReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->negativeTolerance;
}
uint32_t COCOReceiverGetSinglePulseDuration(COCOReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->singlePulseDuration;
}



void COCOMessageSetAddress(COCOMessageRef message, uint32_t address)
{
    assert(NULL != message);
    message->address = address;
}
void COCOMessageSetGroup(COCOMessageRef message, bool group)
{
    assert(NULL != message);
    message->group = group;
}
void COCOMessageSetOnOff(COCOMessageRef message, bool onOff)
{
    assert(NULL != message);
    message->onOff = onOff;
}
void COCOMessageSetChannel(COCOMessageRef message, uint16_t channel)
{
    assert(NULL != message);
    message->channel = channel;
}



