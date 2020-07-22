
#ifndef _GNU_SOURCE
    #define _GNU_SOURCE 1 // to be able to use asprintf()
#endif

#include <stdio.h>
#include <assert.h>
#include "KeyFobSwitchReceiver.h"
#include "PulseRecorder.h"

#if KFSRDebugLogging
    #define DebugLog(format, ...) printf(format, ## __VA_ARGS__)
#else
    #define DebugLog(format, ...)
#endif

/*
This protocol looks like this:
                                                                 |<--- 32 bits in total ---> ........ |       
sync 															 |<-0-bit->|<-1-bit->|
high 						            sync_low					
‾‾| 								                             |‾‾|	   |‾‾‾‾‾‾|  |
  |   								                             |	|	   |      |  |
  |	  								                             |	|	   |      |  |
  |______________________________________________________________|	|______|      |__|
*/

const uint32_t KFSMessageMinPulseCount = 5; // includes sync_low

const uint32_t KFSMessageMaxPulseCount = 67;

// the number of high or low pulses that encode a zero or a one
const uint32_t KFSPulsesPerBit = 2;

// the length of a long pulse, expressed in number of singlepulseDurations
const uint32_t KFSPulsesLong = 3;

// the length of a short pulse, expressed in number of singlepulseDuration
const uint32_t KFSPulsesShort = 1;

// the length of a long start sync pulse, expressed in number of singlepulseDuration
const uint32_t KFSStartSyncLowPulsesCount = 31;

struct KFSMessage 
{
    uint32_t identifier;
    uint8_t  identifierBitSize;
    uint32_t timestamp;
};

struct KFSReceiver 
{
    KFSMessageDetected callback;

    uint32_t repeatCount;       // count of repeated detections of a KFSMessage that should trigger a callback
    uint32_t refractoryPeriod;  // seconds

    uint32_t positiveTolerance; // percentage, e.g. 40 means 40%
    uint32_t negativeTolerance; // percentage, e.g. 40 means 40%
    uint32_t singlePulseDuration; // µicro seconds

    uint32_t timestamp; // timestamp of the end of the long part of the start-sync 
    uint32_t lastTimestamp;
    uint32_t *durations;
    uint32_t durationsIndex;
    uint32_t repeats;
    uint32_t receivedCode;
    uint32_t receivedCodeTimestamp;
    uint32_t codeBitLength;
    uint32_t codePulseLength;
    uint32_t singlePulseMaxDuration;
    uint32_t singlePulseMinDuration;
    uint32_t startSyncLowMinDuration;
    uint32_t startSyncLowMaxDuration;
    uint32_t startTime;
    uint32_t timestampPreviousHit;
    uint32_t previousMessageIdentifier;
    uint32_t previousIdentifierBitSize;

    PulseRecorderRef pulseRecorder;
};

uint32_t KFSMessageGetIdentifier(KFSMessageRef message)
{
    assert(NULL != message);
    return message->identifier;
}

void KFSMessageSetIdentifier(KFSMessageRef message, uint32_t identifier)
{
    assert(NULL != message);
    message->identifier = identifier;
}

void KFSPrintBinary(uint32_t value, int size)
{
    for (size_t index = 1; index <= size; index++)
    {
        int shiftValue = size - index;
        uint32_t bitValue = (value >> shiftValue) & 1;
        char bitRepresentation = bitValue ? '1' : '0';
        printf("%c", bitRepresentation);
        fflush(stdout);
    }
}

void KFSUpdateDurationsForReceiver(KFSReceiverRef receiver)
{
    receiver->singlePulseMinDuration = receiver->singlePulseDuration * (100 - receiver->negativeTolerance) / 100;
    receiver->singlePulseMaxDuration = receiver->singlePulseDuration * (100 + receiver->positiveTolerance) / 100;

    receiver->startSyncLowMinDuration = receiver->singlePulseDuration * KFSStartSyncLowPulsesCount * (100 - receiver->negativeTolerance) / 100;
    receiver->startSyncLowMaxDuration = receiver->singlePulseDuration * KFSStartSyncLowPulsesCount * (100 + receiver->positiveTolerance) / 100;
}

KFSReceiverRef KFSReceiverCreate()
{
    KFSReceiverRef newReceiver = malloc(sizeof(struct KFSReceiver));
    if (NULL != newReceiver)
    {
        // public
        newReceiver->repeatCount = 2;
        newReceiver->refractoryPeriod = 0;

        newReceiver->positiveTolerance = 20;
        newReceiver->negativeTolerance = 20;
        newReceiver->singlePulseDuration = 350;

        KFSUpdateDurationsForReceiver(newReceiver);

        newReceiver->startTime = 0;
        newReceiver->timestampPreviousHit = 0;
        newReceiver->previousMessageIdentifier = 0;
        newReceiver->previousIdentifierBitSize = 0;

        newReceiver->timestamp = 0;

        newReceiver->lastTimestamp = 0;
        newReceiver->durations = malloc(sizeof(uint32_t) * KFSMessageMaxPulseCount);
        newReceiver->durationsIndex = 0;
        newReceiver->repeats = 0;
        newReceiver->receivedCode = 0;
        newReceiver->receivedCodeTimestamp = 0;

    }
    return newReceiver;
}

bool arePulsesValidCode(KFSReceiverRef receiver, uint32_t timestamp, KFSMessageRef message)
{ 
    assert(NULL != receiver);
    assert(NULL != message);

    uint32_t code = 0;
    uint8_t codeLength = 0;

    // skipping the SYNC
    for (int i = 1; i < receiver->durationsIndex; i+=2)
    {
        if (receiver->durations[i] > receiver->singlePulseMinDuration * KFSPulsesShort && receiver->durations[i] < receiver->singlePulseMaxDuration * KFSPulsesShort &&
            receiver->durations[i+1] > receiver->singlePulseMinDuration * KFSPulsesLong && receiver->durations[i+1] < receiver->singlePulseMaxDuration * KFSPulsesLong)
        {
            code <<= 1;
            codeLength += 1;
        }
        else if (receiver->durations[i] > receiver->singlePulseMinDuration * KFSPulsesLong && receiver->durations[i] < receiver->singlePulseMaxDuration *  KFSPulsesLong &&
            receiver->durations[i+1] > receiver->singlePulseMinDuration * KFSPulsesShort && receiver->durations[i+1] < receiver->singlePulseMaxDuration * KFSPulsesShort)
        {
            code <<= 1;
            code |= 1;
            codeLength += 1;
        }
        else
        {
            // these two pulse do not encode a zero or a one
            // end of code
            break;
        }
        
        if (codeLength == 24) 
        { break; }
    }
    
    if (0 != code && codeLength > 4)
    {
        message->identifier = code;
        message->identifierBitSize = codeLength;
        message->timestamp = timestamp;

        if (NULL != receiver->pulseRecorder)
        {   
            char* description;
            int bytesPrinted = asprintf(&description, "code: %lu\nlength: %lu\n", code, codeLength);
            if (bytesPrinted > 0)
            {
                PulseRecorderAddSequenceDescription(receiver->pulseRecorder, description);
            }
            PulseRecorderAddPulses(receiver->pulseRecorder, receiver->durations, codeLength);
        }

        return true;
    }
    return false;
}

void KFSSetRecordReceivedTransmissions(KFSReceiverRef receiver, bool shouldRecord)
{
    // remove existing recorder
    if (NULL != receiver->pulseRecorder)
    {
        PulseRecorderRelease(receiver->pulseRecorder);
        receiver->pulseRecorder = NULL;
    }

    PulseRecorderRef recorder = PulseRecorderCreate("KFSRTransmitRecording.txt");
    if (NULL != recorder)
    {
        receiver->pulseRecorder = recorder;
    }
    else 
    {
        printf("KFSSetRecordReceivedTransmissions: could not create PulseRecorder");
    }
}

KFSMessageRef KFSMessageCreate()
{
    KFSMessageRef message = malloc(sizeof(struct KFSMessage));
    if (NULL != message)
    {  
        message->identifier = 0;
        message->identifierBitSize = 0;
        message->timestamp = 0;
    }
    return message;
}

void KFSReceiverFeedGPIOValueChangeTime(KFSReceiverRef receiver, uint32_t timestamp)
{
    //  timestamp in microseconds:
    uint32_t duration = timestamp - receiver->lastTimestamp;

    if (0 == receiver->lastTimestamp)
    {
        // first callback, no actual duration yet
        receiver->lastTimestamp = timestamp;
        return;
    }

    if (duration > receiver->startSyncLowMinDuration &&
        duration < receiver->startSyncLowMaxDuration)
    {
        receiver->startTime = timestamp;
        
        // start-sync detected. If we were already collecting durations
        // let's analyze what we have so far
        if (receiver->durationsIndex >= KFSMessageMinPulseCount)
        {
            KFSMessageRef message = malloc(sizeof(struct KFSMessage));
            message->identifier = 0;
            message->identifierBitSize = 0;
            message->timestamp = 0;

            bool messageOwnedByUs = true;

            if (arePulsesValidCode(receiver, receiver->startTime, message))
            {
                // code detected
                if (message->identifier != 0 &&
                    message->identifier == receiver->previousMessageIdentifier &&
                    message->identifierBitSize == receiver->previousIdentifierBitSize
                    )
                {
                    receiver->repeats += 1;
                    if (receiver->repeats == receiver->repeatCount)
                    {
                        // only count this as a hit, if the previous hit
                        // was more than 3 seconds ago (this program was written for
                        // a doorbell originally, for (dimming) switches, maybe 
                        // 3-seconds spacing is too much. You might want none, and
                        // just increase the repeats instead.
                        if (message->timestamp - receiver->timestampPreviousHit > (receiver->refractoryPeriod * 1000000))
                        {
                            receiver->timestampPreviousHit = message->timestamp;
                            messageOwnedByUs = false;
                            receiver->repeats = 0;
                            receiver->previousMessageIdentifier = 0;
                            receiver->previousIdentifierBitSize = 0;
                          
                            if (NULL != receiver->callback)
                            {
                                receiver->callback(receiver, message);
                            }
                        }
                    }
                }
                else 
                {
                    receiver->repeats = 0;
                }
                receiver->previousMessageIdentifier = message->identifier;
                receiver->previousIdentifierBitSize = message->identifierBitSize;
            }
            if (messageOwnedByUs)
            {
                free(message);
                message = NULL;
            }
        }
        receiver->durationsIndex = 0;
    }

    if (receiver->durationsIndex >= KFSMessageMaxPulseCount)
    {
        receiver->durationsIndex = 0;
    }

    receiver->durations[receiver->durationsIndex] = duration;

    receiver->durationsIndex += 1;
    receiver->lastTimestamp = timestamp;
}

void KFSMessageRelease(KFSMessageRef message)
{
    assert(NULL != message);
    free(message);
}

void KFSReceiverRelease(KFSReceiverRef receiver)
{
    assert(NULL != receiver);
    if (NULL != receiver->pulseRecorder)
    {
        PulseRecorderRelease(receiver->pulseRecorder);
        receiver->pulseRecorder = NULL;
    }

    free(receiver->durations);
    free(receiver);
}

/// Getting and Setting properties
void KFSReceiverSetCallback(KFSReceiverRef receiver, KFSMessageDetected callback)
{
    assert(NULL != receiver);
    receiver->callback = callback;
}

void KFSReceiverSetRepeatCount(KFSReceiverRef receiver, uint32_t repeatCount)
{
    assert(NULL != receiver);
    receiver->repeatCount = repeatCount;
}

void KFSReceiverSetRefractoryPeriod(KFSReceiverRef receiver, uint32_t refractoryPeriod)
{
    assert(NULL != receiver);
    receiver->refractoryPeriod = refractoryPeriod;
}

void KFSReceiverSetSinglePulseDuration(KFSReceiverRef receiver, uint32_t pulseDuration)
{
    assert(NULL != receiver);
    receiver->singlePulseDuration = pulseDuration;
}

void KFSReceiverSetPositiveTolerance(KFSReceiverRef receiver, uint32_t tolerance)
{
    assert(NULL != receiver);
    receiver->positiveTolerance = tolerance;
}

void KFSReceiverSetNegativeTolerance(KFSReceiverRef receiver, uint32_t tolerance)
{
    assert(NULL != receiver);
    receiver->negativeTolerance = tolerance;
}

// Querying the reeiver.
uint32_t KFSReceiverGetRepeatCount(KFSReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->repeatCount;
}
uint32_t KFSReceiverGetRefractoryPeriod(KFSReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->refractoryPeriod;
}
uint32_t KFSReceiverGetPositiveTolerance(KFSReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->positiveTolerance;
}
uint32_t KFSReceiverGetNegativeTolerance(KFSReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->negativeTolerance;
}
uint32_t KFSReceiverGetSinglePulseDuration(KFSReceiverRef receiver)
{
    assert(NULL != receiver);
    return receiver->singlePulseDuration;
}
