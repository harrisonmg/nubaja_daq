#ifndef NUBAJA_TEMP_DRIVER
#define NUBAJA_TEMP_DRIVER

//standard c shite
#include <stdio.h>


//custom
#include "nubaja_logging.h"
#include "nubaja_adc.h"

//vars
// static const char *NUBAJA_TEMP_DRIVER_TAG = "NUBAJA_TEMP_DRIVER";

void read_temp () { 

    uint16_t adc_raw = adc1_get_raw(TEMP);  //read ADC (thermistor)
    add_12b_to_buffer(f_buf,adc_raw); 
    float adc_v = (float) adc_raw * ADC_SCALE; //convert ADC counts to temperature
    float temp = (adc_v - THERM_B) / THERM_M;
    printf("temp: %f\n",temp);                 
    display_temp(PORT_1,temp);
    
}

#endif