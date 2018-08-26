#ifndef NUBAJA_TEMP_H_
#define NUBAJA_TEMP_H_

#include "driver/adc.h"
#include "esp_adc_cal.h"

// thermistor configs
// pn: NTCALUG02A103F800
#define THERM_M   0.024
#define THERM_B  -0.5371  // linear fit to Vout vs. temp of thermistor circuit
#define THERM_CHANNEL ADC1_CHANNEL_3  // thermistor ADC channel

float read_temp()
{
    uint16_t adc_raw = adc1_get_raw(THERM_CHANNEL);  // read ADC (thermistor)
    float adc_v = (float) adc_raw * ADC_SCALE;  // convert ADC counts to voltage
    float temp = (adc_v - THERM_B) / THERM_M;  // convert voltage to temperature
    return temp;
}

#endif // NUBAJA_TEMP_H_
