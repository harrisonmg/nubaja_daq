#ifndef NUBAJA_RUNMODES
#define NUBAJA_RUNMODES

#include <stdio.h> 

/*
 * Below is the definition of all current run modes. 
 * These can be expanded as necessary
 * Currently, three bits are necessary to control the flow of the program
 * BIT 0 is used to enable or disable logging
 * BIT 1 is used to enable or disable sensors
 * BIT 2 is used to enable or disable WiFi 
 */

typedef enum runmode_t {
	LAB = 0b010,
	FIELD = 0b111,
	DEFAULT = 0b000
} Runmode_t;

#endif