#include "PulseRecorder.h"

struct PulseRecorder
{
	FILE *OUTFILE;
};

PulseRecorderRef PulseRecorderCreate(char * logFilePath)
{
	PulseRecorderRef newRecorder = 	NULL;
	
	FILE * logFile;
	// clear file
    logFile = fopen(logFilePath, "w");

    if (NULL != logFile) { fclose(logFile); }

    logFile = fopen(logFilePath, "a");
    if (NULL == logFile)
    {
        printf("PulseRecorderCreate(): Could not open file `%s`\n", logFilePath);
    }
    else 
    {
    	newRecorder = malloc(sizeof(struct PulseRecorder));
    	if (NULL != newRecorder)
    	{
    		newRecorder->OUTFILE = logFile;
    	}
    	else 
    	{
    		fclose(logFile);
    	}
    }

	return newRecorder;
}
void PulseRecorderRelease(PulseRecorderRef recorder)
{
	if (NULL != recorder->OUTFILE)
    {
        fclose(recorder->OUTFILE);
    }
    free(recorder);
}
void PulseRecorderAddSequenceDescription(PulseRecorderRef recorder, char * description)
{
	fprintf(recorder->OUTFILE, description);
	fflush(recorder->OUTFILE);
}
void PulseRecorderAddPulses(PulseRecorderRef recorder, uint32_t *durations, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++)
    {
        fprintf(recorder->OUTFILE, "[%3lu] %5lu\n", index, durations[index]);
    }
    fflush(recorder->OUTFILE);
}
void PulseRecorderEndSequence(PulseRecorderRef recorder)
{
	fprintf(recorder->OUTFILE, "\n\n");
	fflush(recorder->OUTFILE);
}
