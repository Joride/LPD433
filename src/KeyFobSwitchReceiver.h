#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>

// set this to non-zero to enable extensive printout of received signals
#define KFSDebugLogging 0

// An opaque type on which to operate
typedef struct KFSReceiver  *KFSReceiverRef;
typedef struct KFSMessage   *KFSMessageRef;

typedef void (*KFSMessageDetected)(KFSReceiverRef, KFSMessageRef);

// querying a KFSMessageRef
uint32_t KFSMessageGetIdentifier(KFSMessageRef message);

/*
Releases a KFSMessageRef. The advantage of using this function over
free(), is that this function is save when `receiver` is NULL.
*/
void KFSMessageRelease(KFSMessageRef message);

/*
Creates a new KFSReceiver, or NULL if a receiver could not be created. You are 
responsible for releasing this object using KFSReceiverRelease().
*/
KFSReceiverRef KFSReceiverCreate();

/*
Releases a KFSReceiverRef. 
Warning: do *not* call free() on a KFSReceiverRef, as this will memory leaks.
A KFSReceiverRef maintains some internal structures on the heap, and these are
released in KFSReceiverRelease(), not when calling free().
*/
void KFSReceiverRelease(KFSReceiverRef receiver);

/*
Set a callback to be called when a KFSMessage is detected.
Note:
If your callback is never called, try tweaking some of the parameters.
*/
void KFSReceiverSetCallback(KFSReceiverRef receiver, KFSMessageDetected callback);

/*
Every time the GPIO that you are monitoring changes state, call this function 
the timestammp of that change. 
Note: KFSReceiver was built and tested using PIGPIO, and the timestamp fed
into this function was the timestamp (`tick`) parameter of the PIGPIO callback
set with `gpioSetAlertFunc`.
*/
void KFSReceiverFeedGPIOValueChangeTime(KFSReceiverRef receiver, uint32_t timestamp);

/*
This value defaults to 1: any identical message coming in this number of repeated times
will trigger KFSReceiver to call your callback/
KFS devices (and most other operating in the 433MHz band) send their message 
several times in quick succession, as a poor-man's way of error-correction I
suppose.
The proper value depends on various real-world factors (noise, distance, transmitter, etc).
Generally, the trade of is like this:
higher value: smaller changes of false positives/duplicates, higher chances missing messages
lower value: higher changes of false positives / duplicates, lower changes if missing messages
When trouble-shouting, a low value (e.g. 1) will allow you to see more easily 
if you are detecting anything at all.	
*/
void KFSReceiverSetRepeatCount(KFSReceiverRef receiver, uint32_t repeatCount);

/*
Defaults to 0, expressed in seconds: any times a message is detected with the repeatcount specified by
KFSReceiverSetRepeatCount(), KFSReceiver will call your callback
The time after a message was detected that that an identical message will be ignored,
even if it was repeated for the number of times specified by KFSReceiverSetRepeatCount();
This value allows for a low setting of repeatCount (to not miss a message), while
at the same time avoiding the remainder of that same transmission to be considered
another message. E.g.: a doorbell is unlikely to be pressed more than once every 3 seconds, even
when people are impatient, but the doorbell or your receiver might function unreliably:
sometimes you detect 4 repeats, sometimes 12. If the repeatcount would be 5,
you would catch all messages, but the times 12 are send, you would get 2 callbacks. 
The refractoryPeriod helps you out here.
When troubleshouting, set this to 0, to catch any message, even multiple in  rapidsuccession
*/
void KFSReceiverSetRefractoryPeriod(KFSReceiverRef receiver, uint32_t refractoryPeriod);

/*
Defaults to 260.
The duration of a single-pulse period expressed in microseconds.
Only change this as a last resort (acceptable values seem to range from 200 - 400),
if you are unfamiliar with the KFS (or any On-Off Keying modulation), this
is probably not a value you should change.
*/
void KFSReceiverSetSinglePulseDuration(KFSReceiverRef receiver, uint32_t pulseDuration);

/*
Defaults to 40.
The accepted upward error-range of pulse durations in percent. It will allow for
n% longer pulses in the protocol (where n is the tolerance). 
For instance, the pulses encoding '0' might be:
245   300   255  1345. If the tolerance would be 0, and the singlePulseLength
260, each of these values would be considered invalid. The positive tolerance allows for
the 2nd ('300') and 4th ('1345') value to be considered valid (260 * (1 + 1/tolerance))
and (260 * 4 * (1 + 1/tolerance)). First and third value would need a negative tolerance
to be considered valid.
*/
void KFSReceiverSetPositiveTolerance(KFSReceiverRef receiver, uint32_t tolerance);

/*
Defaults to 40.
The accepted downward error-range of pulse durations in percent. It will allow for
n% shorter pulses in the protocol (where n is the tolerance). 
For instance, the pulses encoding '0' might be:
245   300   255  1345. If the tolerance would be 0, and the singlePulseLength
260, each these values would be considered invalid. The negative tolerance allows for
the 1st ('245') and 3rd ('255') value to be considered valid (260 * (1 - 1/tolerance)).
Second and 4th value would need a positive tolerance to be considered valid.
*/
void KFSReceiverSetNegativeTolerance(KFSReceiverRef receiver, uint32_t tolerance);

// Querying the reeiver.
uint32_t KFSReceiverGetRepeatCount(KFSReceiverRef receiver);
uint32_t KFSReceiverGetRefractoryPeriod(KFSReceiverRef receiver);
uint32_t KFSReceiverGetPositiveTolerance(KFSReceiverRef receiver);
uint32_t KFSReceiverGetNegativeTolerance(KFSReceiverRef receiver);
uint32_t KFSReceiverGetSinglePulseDuration(KFSReceiverRef receiver);


////
// These methods allow you to create a KFSMessage to send out

/*
Creates a new KFSMessageRef, or NULL if a receiver could not be created. You are 
responsible for releasing this object using KFSMessageRelease().
*/
KFSMessageRef KFSMessageCreate();

void KFSMessageSetIdentifier(KFSMessageRef message, uint32_t identifier);

// default is `false`
void KFSSetRecordReceivedTransmissions(KFSReceiverRef receiver, bool shouldRecord);
