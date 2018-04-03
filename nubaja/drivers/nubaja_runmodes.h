#ifndef NUBAJA_RUNMODES
#define NUBAJA_RUNMODES

#include <stdio.h> 

/*
 * Below is the definition of all current run modes. 
 * These can be expanded as necessary
 * Currently, four bits are necessary to control the flow of the program
 * BIT 0 is used to enable or disable logging
 * BIT 1 is used to enable or disable sensors
 * BIT 2 is used to enable or disable WiFi 
 * BIT 3 is used to enable or disable error recording 
 */

typedef enum runmode_t {
	LAB = 0b0010,
	LAB_LOG = 0b0011,
	LAB_LOG_ERR = 0b1011,
	LAB_WIFI = 0b0110,
	FIELD = 0b1111,
	DEFAULT = 0b000
} Runmode_t;

#endif