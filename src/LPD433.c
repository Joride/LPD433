/*
The PIGPIO library that is used in this program requires root-level priviliges.
After building, either run the compiled program as root, or change the 
file mode and ownership of the program:

# change owner of both user and group to `root`:
    > `sudo chown root:root main` 

# Change file mode for user: read, write execute; for group and others: read, execute
The additional `4` sets the sticky bit to 0, SGID to 0 and the SUID to 1.
With the SUID set to 1 the script will run as the owner (wich was just set to 
`root`). This will avoid requiring the caller of this program to have root
priviliges.
    > `sudo chmod 4755 main`
*/

#include <stdio.h>
#include <pigpio.h>
#include "COCOReceiver.h"
#include "KeyFobSwitchReceiver.h"
#include "OOKSender.h"
#include <unistd.h> // sleep()
#include <string.h> // strcmp()
#include <ctype.h> // isspace()

void printUsage();

typedef enum Mode 
{
    OperationModeUnknown = 0,
    OperationModerReceiving = 1,
    OperationModerSending = 2
} OperationMode;

OperationMode mode = OperationModeUnknown;

// flipped to `false` upon the user hitting <enter>, causing the program to end
// when in receiver mode
bool receiving = true; 

// the PIN to use for either receiving or sending
int PIN = 0;

// the protocol to use when in sending mode (`COCO` or `KFS`)
char * protocol = NULL;

// KFS identifier
uint32_t identifier = 0;

// COCO properties
uint32_t address = 0;
bool onOff = 0;
bool group = 0;
uint16_t channel = 0;

// receivers. These are given the timestamp of GPIO changes and will try and 
// detect messages of a specific protocol, and callback once such a message is 
// detected.
COCOReceiverRef COCOReceiver = NULL;
KFSReceiverRef KFSReceiver = NULL;

// PIGPIO-callback. 
void gpioValueChanged(int gpio, int level, uint32_t timestamp)
{
    // timestamp is simply forwarde to the receivers
    COCOReceiverFeedGPIOValueChangeTime(COCOReceiver, timestamp);
    KFSReceiverFeedGPIOValueChangeTime(KFSReceiver, timestamp);
}

// COCO receiver callback
void COCOCallback(COCOReceiverRef receiver, COCOMessageRef message)
{
    // a COCO message was detected
    printf("\n╔═════ COCO Message ═════╗\n║ address:\t%8lu ║\n║ group:\t%8i ║\n║ onOff:\t%8i ║\n║ channel:\t%8i ║\n╚════════════════════════╝\n", 
        COCOMessageGetAddress(message),
        COCOMessageGetGroup(message),
        COCOMessageGetOnOff(message),
        COCOMessageGetChannel(message) );

    COCOMessageRelease(message);
}

void KFSCallback(KFSReceiverRef receiver, KFSMessageRef message)
{
    // a KFSR message was detected
    printf("\n╔════ KeyFob Message ════╗\n║ identifier:\t%8lu ║\n╚════════════════════════╝\n", 
        KFSMessageGetIdentifier(message) );

    KFSMessageRelease(message);
}

char * trimWhitespacesFromString(char * string)
{
    // Trim leading space
    while(isspace((unsigned char) *string)) 
    { string += 1; }

    // the entire string is made up of whitespace
    // return a pointer to the terminating NULL char
    if(*string == '\0') 
    { return NULL; }

    // Trim trailing space
    char *end = string + strlen(string) - 1;
    while((end > string) && isspace((unsigned char) *end)) 
    { end -= 1; }

    end[1] = '\0';

    return string;
}

bool parseArgs(int argc, char *argv[])
{
    if (argc < 3) 
    {
        printf("ERROR: incorect number of arguments.\n");
        return false; 
    }

    PIN = atoi(argv[1]);

    // strcmp returns 0 when strings are equal
    if (!strcmp(argv[2], "-s")) 
    {
        mode = OperationModerSending;
        if (5 != argc)
        {
            printf("Incorrect number of arguments for sending a message. Expecting: PIN -s PROTOCOL \"[key value...]\". Did you forget quotes around the key-value array?\n");
            return false;
        }

        // get protocol
        protocol = argv[3];
        // parse the keyvalue array
        char *keyValues = argv[4];
        if ('[' != keyValues[0])
        {
            printf("Error: expected opening of array with [, got %c.\n", keyValues[0]);
            return false;
        }

        // KFS values
        identifier = 0;
        uint32_t * identifierPtr = NULL;

        // COCO values
        address = 0;
        onOff = 0;
        group = 0;
        channel = 0;
        uint32_t * addressPtr = NULL;
        bool * onOffPtr = NULL;
        bool * groupPtr = NULL;
        uint16_t * channelPtr = NULL;

        char * currentKey = NULL;
        char *keyValuePair;
        while ((keyValuePair = strsep(&keyValues, ","))) 
        {
            char * trimmedKeyValuePair = trimWhitespacesFromString(keyValuePair);
            if (NULL != trimmedKeyValuePair)
            {
                char *keyOrValue;
                while ((keyOrValue = strsep(&keyValuePair, " "))) 
                {
                    char * trimmedKeyOrValue = trimWhitespacesFromString(keyOrValue);
                    if (NULL != trimmedKeyOrValue)
                    {
                        if (!strcmp("[", trimmedKeyOrValue) || !strcmp("]", trimmedKeyOrValue))
                        { /* noting to do, just the opening or closing of the array */ }
                        else 
                        {
                            if (trimmedKeyOrValue[0] == '[')
                            { trimmedKeyOrValue += 1; }
                            
                            int length = strlen(trimmedKeyOrValue);
                            if (trimmedKeyOrValue[length - 1] == ']')
                            { trimmedKeyOrValue[length - 1] = '\0'; }

                            if (NULL == currentKey)
                            { currentKey = trimmedKeyOrValue; }
                            else 
                            {
                                if (!strcmp(currentKey, "identifier"))
                                {
                                    identifier = atoi(trimmedKeyOrValue);
                                    identifierPtr = &identifier;
                                }
                                else if (!strcmp(currentKey, "address"))
                                {
                                    address = atoi(trimmedKeyOrValue);
                                    addressPtr = &address;
                                }
                                else if (!strcmp(currentKey, "onOff"))
                                {
                                    onOff = atoi(trimmedKeyOrValue);
                                    onOffPtr = &onOff;
                                }
                                else if (!strcmp(currentKey, "group"))
                                {
                                    group = atoi(trimmedKeyOrValue);
                                    groupPtr = &group;
                                }
                                else if (!strcmp(currentKey, "channel"))
                                {
                                    channel = atoi(trimmedKeyOrValue);
                                    channelPtr = &channel;
                                }
                                else 
                                {
                                    printf("Error: unkown key in key-value list: %s\n", currentKey);
                                    return false;
                                }
                                currentKey = NULL;
                            }
                        }
                    }
                }
                if (NULL != currentKey)
                {
                    printf("Error: extraneous token found: `%s`. Did you forget to specify a key or its value?\n", currentKey);
                    return false;
                }
            }
        }

        if (!strcmp(protocol, "KFS"))
        {
            if (NULL == identifierPtr)
            {
                printf("Error: no key-value specified for identifier.\n");
                return false;
            }
            return true;
        }
        else if (!strcmp(protocol, "COCO"))
        {
            bool shouldExit = false;
            if (NULL == addressPtr)
            {
                printf("Error: no key-value specified for address.\n");
                shouldExit = true;
            }
            if (NULL == onOffPtr)
            {
                printf("Error: no key-value specified for onOff.\n");
                shouldExit = true;
            }
            if (NULL == groupPtr)
            {
                printf("Error: no key-value specified for group.\n");
                shouldExit = true;
            }
            if (NULL == channelPtr)
            {
                printf("Error: no key-value specified for channel.\n");
                shouldExit = true;
            }
            
            if (shouldExit) { return false; }           

            return true;
        }
    }
    else if (!strcmp(argv[2], "-r"))
    {
        if (argc > 3) 
        {
            printf("ERROR: too many arguments.\n");
            return false; 
        }
        mode = OperationModerReceiving;
        return true; 
    }
    else 
    {
        printf("Incorrect 2nd argument. Expected \"-r\" or \"-s\", but got \"%s\".", argv[1]);
        return false;
    }
	return false;
}

void sendCOCOMessage(int PIN, uint32_t address, bool onOff, bool group, uint16_t channel)
{
    COCOMessageRef message = COCOMessageCreate();
    COCOMessageSetAddress(message, address); // 26541806
    COCOMessageSetGroup(message, group);
    COCOMessageSetOnOff(message, onOff);
    COCOMessageSetChannel(message, channel);

    OOKSenderRef sender = OOKSenderCreate();
    OOKSenderSetTransmitGPIO(sender, PIN);

    printf("Sending COCO message with address = %lu, group = %u, onOff = %u, channel = %u\n", 
            COCOMessageGetAddress(message),
            COCOMessageGetGroup(message),
            COCOMessageGetOnOff(message),
            COCOMessageGetChannel(message));
    OOKSenderSendCOCO(sender, message);

    // cleanup
    OOKSenderRelease(sender);
    COCOMessageRelease(message);
}
void sendKFSMessage(int PIN, uint32_t identifier)
{
    KFSMessageRef message = KFSMessageCreate();
    KFSMessageSetIdentifier(message, identifier);

    OOKSenderRef sender = OOKSenderCreate();
    OOKSenderSetTransmitGPIO(sender, PIN);

    printf("Sending KFSMessage with identifier = %lu ...\n", identifier);
    OOKSenderSendKFS(sender, message);

    // cleanup
    OOKSenderRelease(sender);
    KFSMessageRelease(message);
}

int main(int argc, char *argv[]) 
{	
    if (parseArgs(argc, argv))
    {
        if (gpioInitialise() == PI_INIT_FAILED)
        {
            fprintf(stderr, "pigpio initialisation failed.\n");
            return 1;
        }
        else
        {
            switch (mode)
            {
                case OperationModeUnknown:
                    // somethiing went wrong, there shoud be a mode after
                    // succesfull parsing or arguments
                    printf("Programmer error: unknonw operation mode.\n");
                    exit(1);
                    break;
                case OperationModerSending:
                {
                    gpioSetMode(PIN, PI_OUTPUT);
                    if (!strcmp(protocol, "COCO"))
                    {
                        sendCOCOMessage(PIN, address, onOff, group, channel);
                    }
                    else if (!strcmp(protocol, "KFS"))
                    {
                        sendKFSMessage(PIN, identifier);
                    }
                    break;
                }
                case OperationModerReceiving:
                {
                    printf("Listening on PIN %i...\n", PIN);

                    COCOReceiver = COCOReceiverCreate();
                    COCOReceiverSetCallback(COCOReceiver, &COCOCallback);
                    COCOReceiverSetRefractoryPeriod(COCOReceiver, 0);
                    COCOReceiverSetRepeatCount(COCOReceiver, 1);
                    // the next line could be usefull for debugging
                    // COCOReceiverSetRecordReceivedTransmissions(COCOReceiver, true);

                    KFSReceiver = KFSReceiverCreate();
                    KFSReceiverSetCallback(KFSReceiver, &KFSCallback);
                    KFSReceiverSetRefractoryPeriod(KFSReceiver, 0);
                    KFSReceiverSetRepeatCount(KFSReceiver, 1);
                    // the next line could be usefull for debugging
                    // KFSSetRecordReceivedTransmissions(KFSReceiver, true);

                    
                    gpioSetMode(PIN, PI_INPUT);
                    gpioSetAlertFunc(PIN, gpioValueChanged);

                    char input[20];
                    printf("Type <enter> to stop listening and exit the program.\n");
                    fgets(input,20,stdin);

                    if (strcmp(input, ""))
                    { receiving = false; }

                    // loop forever until `receiving` gets flipped
                    while (receiving) { ; }

                    // cleanup
                    gpioSetAlertFunc(PIN, NULL);    
                    KFSReceiverRelease(KFSReceiver);
                    COCOReceiverRelease(COCOReceiver);
                    break;
                }
            }
        }
    }
    else 
    {
    	printf("Usage:\n");
    	printUsage();
    	exit(1);
    }

	return 0;
}

void printUsage()
{
	// Reset all attributes: "\e[0m"
	// Bold: "\e[1m"
	printf("\
\e[1mLPD433\e[0m\n\
\n\
\e[1mNAME\e[0m\n\
    LPD433 - (\e[1mL\e[0mow \e[1mP\e[0mower \e[1mD\e[0mevice \e[1m433\e[0mMHz) send or receive messages in the 433MHz band\n\
\n\
\e[1mSYNOPSIS\e[0m\n\
    LPD433 -r PIN\n\
    LPD433 -s PIN PROTOCOL \"[messageField value, ...]\"\n\
\n\
\e[1mDESCRIPTION\e[0m\n\
    433MHz send and/or receive hardware is required to be connected to the Raspberry Pi's GPIO pins.\n\
    This program can send  a message in accordance with the ClickOnClickOff protocol, or a protocol that the author reverse engineered from a certain\n\
    type of CarKeyFob-like remotes.\n\
\n\
\e[1mOPTIONS\e[0m\n\
    -s  PIN PROTOCOL [messageField value, ...]\n\
        Send a message on the GPIO PIN specified. PROTOCOL should be either `COCO`, for a ClickOnClickOff messate, or KFS for a KeyFobSwitch\n\
        message. All fields are required. Fields and valuetypes:\n\
        COCO: \"[address <26 bit unsigned integer>, onOff <1 or 0>, group, <1 or 0, channel <16bit unsigned integer>]\"\n\
        KFS:  \"[identifier, <24 bit unsigned integer>]\"\n\
        N.b. the array of messageField names and values \e[4mmust\e[0m be enclosed in quotes.\n\
    -r  PIN\n\
        Receive messages. Details of the messages are printed to the standard output. PIN is a required number that specifies through which GPIO pin the message needs to be received. The program will run until you hit <enter>, or use CTRL-C.\n\
\n\
\e[1mAuthor\e[0m\n\
    LPD433 is written and maintained by Jorrit van Asselt, \e[4mhttps://github.com/Joride/\e[0m.\n\
    July 21, 2020.\n\
\n\
\e[1mEXAMPLES\e[0m\n\
    Send a COCO message with address 235498, onOff = On, group = Off and channel 4598:\n\
    LPD433 27 -s \"[address 235498, onOff 1, group, 0, channel 4598]\"\n\
\n\
    Send a KFS message with identfier 235498:\n\
    LPD433 27 -s \"[identifier 235498]\"\n\
\n\
\n\
\e[1mLPD433\e[0m\n\
");
}
