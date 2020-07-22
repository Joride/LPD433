#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <pigpio.h> // install this library by running: `sudo apt-get install pigpio`
#include "COCOReceiver.h"
#include "KeyFobSwitchReceiver.h"

// Set this to `1` to have the sender output some info that might help in debugging
#define OOKSenderDebugLogging 0

typedef struct OOKSender *OOKSenderRef;

/**
Returns an OOKSenderRef. You are responsible for freeing up the memory after
you are done with the OOKSenderRef by calling OOKSenderRelease(). 
Do not use free() directly, as OOKSenderRelease() also frees some any internal 
memorystructures.
*/
OOKSenderRef OOKSenderCreate();

/**
Releases an OOKSenderRef.
Do not use free() directly, as OOKSenderRelease() also frees some any internal 
memorystructures.
*/
void OOKSenderRelease();

/**
Set the PIN number on which to output the pulses. Default PIGPIO numbering.
*/
void OOKSenderSetTransmitGPIO(OOKSenderRef sender, uint8_t GPIO);

/**
This function will send the COCOMessageRef according to the COCO protocol.
This function blocks until the message has been sent (~72ms).
*/
void OOKSenderSendCOCO(OOKSenderRef sender, COCOMessageRef message);

/**
This function will send the KFSMessageRef according to a specific KFS protocol,
one that works with most key fob switches that can be found by searching
for "Car Key Led Dimmer met RF Key Remote, 8A, 12V-24V"
This function blocks until the message has been sent (~45ms).
*/
void OOKSenderSendKFS(OOKSenderRef sender, KFSMessageRef message);
