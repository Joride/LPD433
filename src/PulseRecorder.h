#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>


typedef struct PulseRecorder *PulseRecorderRef;

/**
Creates a new PulseRecorderRef. You are responsible for releasing this object
via `PulseRecorderRelease()`. Don't use `free()`, as  `PulseRecorderRelease()`
also frees some internal structures.
*/
PulseRecorderRef PulseRecorderCreate(char * logFilePath);

/**
Frees the argument and all of its internal structures.
*/
void PulseRecorderRelease(PulseRecorderRef recorder);

/**
Add a description the the log. This is written to the output immediately.
*/
void PulseRecorderAddSequenceDescription(PulseRecorderRef recorder, char * description);

/**
The recorder will write the durations to the output immediately.
*/
void PulseRecorderAddPulses(PulseRecorderRef recorder, uint32_t *durations, uint32_t length);

/**
This function is usefull when logging multiple sequences: it marks the ned
of one sequence by adding two newlines.`
*/
void PulseRecorderEndSequence(PulseRecorderRef recorder);
