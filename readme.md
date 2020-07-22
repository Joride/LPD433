## This program allows you to receive and send ClickOnClickOff and specific KeyFob Remote signals
	Specifically: in receiver mode you can see printed details of these signals,
	which you can then write down and use to send out yourself.
	You want to control a switch? 
		* Use this prorgram in receiver mode to detect the details of the switch (it has to be ClickOnClickOff, or the right type of keyfob remote)
		* write down the received details
		* run this program in sender mode using those details

## How to get this to work on your Raspberry Pi:

# Caveats
	- This program can either send, or receive. It cannot do both. If you are so
	inclined, you could offcourse modify it yourself to do both.
	- You cannot run two instances of this program to get around the previous
	point: the PIGPIO library that is used within this program only allows one
	running instance. Again, if you are so inclined, you can modify this program
	to change that behaviour.

# Procure some non-heterodyne (the cheapest) 433Mhz transmitters / receivers
	These can be purchased for about about €1 a piece, or less. The signal range of these items is pretty terrible: a few cm at most. You can solder an anttenna on to the transmitter. I managed to get it reach for about 10 meters to activate an actual ClickOnClickOff Doorbell. As for the receiver: I tried various ways of soldering various antennas (dipole, helical, a wire taken from said doorbell), none of which seemed to be able to increaese the receiver range beyond 1 - 1.5 meter.

# Connect the receiver and/or transmitter to the GPIO of your Raspberry Pi
	- VCC to a 3.3V or 5V PIN (check specs of your receiver / transmitter)
	- ground to a ground PIN
	- DATA to a data PIN of your choice. You need to write down the number as you will need it when running this program. Receivers tend to have two DATA pins, you only need to connect one.

# Get the required software libraries
	* GCC - to compile the c-files
		> `sudo apt-get install gcc`
	* pigpio - required library for the program to be able to interact with the GPIO pins
		> `sudo apt-get install pigpio`

# Run the included build script from the toplevel directory of this repository
	This is actually a plain text file that is executable, and is included only for your convenience.
	`sudo buildandrun`
	It will do the following:
	1. it will compile the .c-files into a binary
	2. create a `build` directory in which the produced binary will be placed
	3. it will run the binary with the proper arguments to send an example ClickOnClickOff message through pin 17
	4. it will run the binary with the proper arguments to send an example KeyFobSwitch message through pin 17
	5. it will run the binary in receiver-mode on pin 27


Jorrit van Asselt, July 21st, 2020