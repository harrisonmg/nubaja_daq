#ifndef NUBAJA_TEMP_DRIVER
#define NUBAJA_TEMP_DRIVER

//standard c shite
#include <stdio.h>


//custom
#include "nubaja_logging.h"
#include "nubaja_adc.h"

//vars
// static const char *NUBAJA_TEMP_DRIVER_TAG = "NUBAJA_TEMP_DRIVER";

//THERMISTOR CONFIGS 
#define THERM_M                             0.024                    
#define THERM_B                             -0.5371 //(y=mx + b, linear fit to Vout vs. temperature of thermistor circuit)
#define TEMP                                ADC1_CHANNEL_3
//thermistor pn: NTCALUG02A103F800

void read_temp () { 

    uint16_t adc_raw = adc1_get_raw(TEMP);  //read ADC (thermistor)
    add_12b_to_buffer(f_buf,adc_raw); 
    float adc_v = (float) adc_raw * ADC_SCALE; //convert ADC counts to temperature
    float temp = (adc_v - THERM_B) / THERM_M;
    printf("temp: %f\n",temp);                 
    display_temp(PORT_1,temp);
    
}

#endif