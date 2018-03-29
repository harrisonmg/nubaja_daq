#ifndef NUBAJA_ADC
#define NUBAJA_ADC

#define V_REF                               1000
#define V_FS                                3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE                           (V_FS / 4096)
#define ATTENUATION                         ADC_ATTEN_11db

#include "esp_adc_cal.h"
#include "driver/adc.h"
#include <stdarg.h>
#include "nubaja_logging.h"

/*
* function designed with variable number of arguments
* allows for adc reads of multiple channels without
* several repetitive function calls
* function reads a single channel from the adc
* the raw value is from 0-4095 (12b resolution)
* the raw value is then added to the data buffer appropriately
*/
void read_adc1(int num,...) 
{  
    va_list valist;
    uint16_t val_0;

    /* initialize valist for num number of arguments */
    va_start(valist, num);

    /* access all the arguments assigned to valist */
    for (int i = 0; i < num; i++) {
        val_0 = adc1_get_raw(va_arg(valist, int));
        // printf("counts: %03x\n",val_0); 
        add_12b_to_buffer(f_buf,val_0);        
    }

    /* clean memory reserved for valist */
    va_end(valist);    
}

#endif