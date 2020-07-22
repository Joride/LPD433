#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <assert.h>
#include "OOKSender.h"

struct OOKSender
{
	uint8_t GPIO;

#if OOKSenderDebugLogging
	FILE * OUTFILE;
#endif
};

void OOKSenderPrintBinary(uint32_t value, int size)
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

uint64_t timeInMicroSeconds()
{
	struct timeval t;
	if (gettimeofday(&t, 0) == -1)
	{
		printf("ERROR: could not get timeofday.\n");
		exit(1);
	}
	uint64_t microseconds = (uint64_t) t.tv_usec;
	uint64_t seconds = (uint64_t) ((uint64_t)(t.tv_sec) * 1000000);
	uint64_t timeInMicroSeconds =  seconds + microseconds;

	return timeInMicroSeconds;
}

void printTime()
{
	int hours, minutes, seconds, day, month, year;

	// time_t is arithmetic time type
	time_t now;

	// Obtain current time
	// time() returns the current time of the system as a time_t value
	time(&now);

	// Convert to local time format and print to stdout
	printf("%s\n", ctime(&now));
}

OOKSenderRef OOKSenderCreate()
{
	OOKSenderRef sender = malloc(sizeof(struct OOKSender));
	if (NULL != sender)
	{
		sender->GPIO = 0xFF; // nonsense value

#if OOKSenderDebugLogging
		// clear file
		sender->OUTFILE = fopen("OOKSenderDebugLog.txt", "w");

		if (NULL != sender->OUTFILE)
		{
			fclose(sender->OUTFILE);
			// open for appending
			sender->OUTFILE = fopen("OOKSenderDebugLog.txt", "a");
		}
		
		if (NULL == sender->OUTFILE)
		{
			printf("Could not open file\n");
		}
#endif
		
	}
	return sender;
}
void OOKSenderRelease(OOKSenderRef sender)
{
	assert(NULL != sender);
#if OOKSenderDebugLogging
	fclose(sender->OUTFILE);
#endif
	free(sender);
}
void OOKSenderSetTransmitGPIO(OOKSenderRef sender, uint8_t GPIO)
{
	assert(NULL != sender);
	sender->GPIO = GPIO;
	gpioSetMode(GPIO, PI_OUTPUT);
}
void OOKSenderTransmit(OOKSenderRef sender, 
					   uint32_t *durations, 
					   uint32_t length, 
					   bool firstValueHigh,
					   uint32_t repeats)
{
#if OOKSenderDebugLogging

	// ╔ ╗ ╚ ╝ ║  ═
	fprintf(sender->OUTFILE,"╔═══════════════ OOKSenderDebugLogging ═══════════════╗\n");

	fprintf(sender->OUTFILE,"║ Transmitting %3lu pulses,                            ║\n║ repeating %2lux (i.e. %2lux sent in total).             ║\n║ Alternating pulses, first one is GPIO-%s.         ║\n║ Here is a listing with details:                     ║\n\n",
		length, repeats, repeats + 1, firstValueHigh ? "HIGH" : "LOW  ");
#endif

	assert(NULL != sender);
	assert(sender->GPIO != 0xFF);
	
	if (sender->GPIO == 0xFF) 
	{
		printf("OOKSender: GPIO not set. Please call OOKSenderSetTransmitGPIO() before sending.\n"); 
		return; 
	}

#if OOKSenderDebugLogging
	uint8_t **repeatedLevels = malloc(sizeof(uint8_t *) * (repeats + 1));
	uint64_t **repeatedActualDurations = malloc(sizeof(uint64_t *) * (repeats + 1));
	uint64_t **repeateDeltas = malloc(sizeof(uint64_t *) * (repeats + 1));

	for (uint32_t index = 0; index <= repeats; index ++)
	{
		repeatedLevels[index] = malloc(sizeof(uint8_t) * length);
		repeatedActualDurations[index] = malloc(sizeof(uint64_t) * length);
		repeateDeltas[index] = malloc(sizeof(uint64_t) * length);
	}
#endif

	for (uint32_t repeatIndex = 0; repeatIndex <= repeats; repeatIndex++)
	{	
		uint8_t level = firstValueHigh ? 1 : 0;
#if OOKSenderDebugLogging
		uint64_t previousStartTime = 0;
#endif
		for (uint32_t index = 0; index < length; index++)
		{
			uint64_t startTime = timeInMicroSeconds(); 
			gpioWrite(sender->GPIO, level);

#if OOKSenderDebugLogging
			repeatedLevels[repeatIndex][index] = level;
			uint64_t actualDuration = startTime - previousStartTime;			
			previousStartTime = startTime;

			if (index > 0)
			{
				uint64_t delta = actualDuration > durations[index-1] ? 
										actualDuration - durations[index-1] : 
										durations[index-1] - actualDuration;

				repeatedActualDurations[repeatIndex][index-1] = actualDuration;
				repeateDeltas[repeatIndex][index-1] = delta;
			}
#endif

			// busywait is more accurate than some form of sleep()			
			volatile uint64_t endTime = startTime + durations[index];
			while (timeInMicroSeconds() < endTime) { ; }

			// update for next round
			level = (level == 0) ? 1 : 0;
		}

#if OOKSenderDebugLogging
		uint64_t startTime = timeInMicroSeconds(); 
		uint64_t actualDuration = startTime - previousStartTime;
		uint64_t delta = actualDuration > durations[length-1] ? 
										actualDuration - durations[length-1] : 
										durations[length-1] - actualDuration;

		repeatedActualDurations[repeatIndex][length-1] = actualDuration;
		repeateDeltas[repeatIndex][length-1] = delta;
#endif
	}

#if OOKSenderDebugLogging
	fprintf(sender->OUTFILE, "  index\t GPIO\tT target(µs)\tT actual (µs)\t  ΔT\n");
	for (uint32_t repeatIndex = 0; repeatIndex <= repeats; repeatIndex++)
	{	
		fprintf(sender->OUTFILE, "  ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾  \n");
		uint8_t *levels =  repeatedLevels[repeatIndex];
		uint64_t *actualDurations = repeatedActualDurations[repeatIndex];
		uint64_t *deltas =  repeateDeltas[repeatIndex];
		for (uint32_t index = 0; index < length; index++)
		{
			fprintf(sender->OUTFILE, "  [%3i]\t%5i\t%12lu\t%13llu\t%4llu\n", index, levels[index], durations[index], actualDurations[index], deltas[index]);
		}
		free(levels);
		free(actualDurations);
		free(deltas);
	}
	free(repeatedLevels);
	free(repeatedActualDurations);
	free(repeateDeltas);
#endif

	// turn off the transmitter
	gpioWrite(sender->GPIO, 0);
}

void OOKSenderSendCOCO(OOKSenderRef sender, COCOMessageRef message)
{
	uint32_t address = COCOMessageGetAddress(message);
	bool group = COCOMessageGetGroup(message);
	bool onOff = COCOMessageGetOnOff(message);
	uint16_t channel = COCOMessageGetChannel(message);

	// 26-bit address | 1-bit group | 1-bit on/off | 4-bit channel | stop-sync 
	// bit 6 - 32 are the address
	// bit 5 is the group value
	// bit 4 is the onOff value
	// least significant 4 bits are the channel

	uint32_t fullCode = address;
	
	fullCode <<= 1;
	fullCode |= group ? 1 : 0;

	fullCode <<= 1;
	fullCode |= onOff ? 1 : 0;

	fullCode <<= 4;
	fullCode |= channel;

	#define kCOCOSinglePulseLength 260
	#define kCOCOTotalNumberOfPulses 132
	uint32_t durations[kCOCOTotalNumberOfPulses];

	durations[0] = kCOCOSinglePulseLength;
	durations[1] = 10 * kCOCOSinglePulseLength;
	durations[kCOCOTotalNumberOfPulses - 2] =  1 * kCOCOSinglePulseLength;
	durations[kCOCOTotalNumberOfPulses - 1] = 40 * kCOCOSinglePulseLength;

	for (uint32_t index = 2; index < kCOCOTotalNumberOfPulses - 2; index += 4)
	{
		// most-significant bit gets sent first
		uint8_t shiftValue = 31 - (index - 2) / 4;
		uint8_t bitValue = (fullCode >> shiftValue) & 1;
		if (0 == bitValue)
		{
			durations[index]     = 1 * kCOCOSinglePulseLength;
			durations[index + 1] = 1 * kCOCOSinglePulseLength;
			durations[index + 2] = 1 * kCOCOSinglePulseLength;
			durations[index + 3] = 4 * kCOCOSinglePulseLength;
		}
		else if (1 == bitValue)
		{
			durations[index]     = 1 * kCOCOSinglePulseLength;
			durations[index + 1] = 4 * kCOCOSinglePulseLength;
			durations[index + 2] = 1 * kCOCOSinglePulseLength;
			durations[index + 3] = 1 * kCOCOSinglePulseLength;
		}
		else 
		{
			printf("Programming error: value should be zero or one");
		}
	}

	OOKSenderTransmit(sender, 
					  durations, 
					  kCOCOTotalNumberOfPulses, 
					  true,
					  15);

	#undef kCOCOSinglePulseLength
	#undef kCOCOTotalNumberOfPulses
}

void OOKSenderSendKFS(OOKSenderRef sender, KFSMessageRef message)
{
	uint32_t identifier = KFSMessageGetIdentifier(message);

	#define kKFSSinglePulseLength 350
	#define kKFSTotalNumberOfPulses 50 // 24 bits, 2 pulses per bit, 2 pulses for the start sync
	uint32_t durations[kKFSTotalNumberOfPulses];

	durations[0] = kKFSSinglePulseLength;
	durations[1] = 31 * kKFSSinglePulseLength;
	
	for (uint32_t index = 2; index < kKFSTotalNumberOfPulses; index += 2)
	{
		// most-significant bit gets sent first
		uint8_t shiftValue = 23 - ((index - 2) / 2);
		uint8_t bitValue = (identifier >> shiftValue) & 1;
		if (0 == bitValue)
		{
			durations[index]     = 1 * kKFSSinglePulseLength;
			durations[index + 1] = 3 * kKFSSinglePulseLength;
		}
		else if (1 == bitValue)
		{
			durations[index]     = 3 * kKFSSinglePulseLength;
			durations[index + 1] = 1 * kKFSSinglePulseLength;
		}
		else 
		{
			printf("Programming error: value should be zero or one");
		}
	}

	OOKSenderTransmit(sender, 
					  durations, 
					  kKFSTotalNumberOfPulses, 
					  true,
					  6);

	#undef kKFSSinglePulseLength
	#undef kKFSTotalNumberOfPulses
}

