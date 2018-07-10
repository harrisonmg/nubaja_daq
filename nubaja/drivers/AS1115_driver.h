#ifndef AS1115_DRIVER
#define AS1115_DRIVER

#include "nubaja_i2c_driver.h"

//DISPLAY 
#define DIGIT_0                             0x1 
#define DIGIT_1                             0x2
#define DIGIT_2                             0x3
#define DIGIT_3                             0x4
#define AS1115_SLAVE_ADDR                   0x3
#define BRIGHTNESS                          0xf

static const char *AS1115_DRIVER_TAG = "AS1115_DRIVER";

/*
* configures the AS1115 7-segment display driver to work with HDSP-D03E common anode 4 digit display
*/
void AS1115_config (int port_num) {

    ESP_LOGI(AS1115_DRIVER_TAG,"AS1115 config");
    i2c_write_byte(port_num, 0x0,0x2d,0x1); //enable self addressing setting the slave-addr to 0x03   
    i2c_write_byte(port_num, 0x0,0xc,0x81); //sets shutdown register for normal operation
    i2c_write_byte(port_num, 0x0,0xe,0x04); //sets features as desired with hex-code font
    i2c_write_byte(port_num, AS1115_SLAVE_ADDR,0xc,0x81); //sets shutdown register for normal operation
    i2c_write_byte(port_num, AS1115_SLAVE_ADDR,0xe,0x04); //sets features as desired with hex-code font
    i2c_write_byte(port_num, AS1115_SLAVE_ADDR,0x9,0xff); //decode mode enabled for all digits
    i2c_write_byte(port_num, AS1115_SLAVE_ADDR,0xa,BRIGHTNESS); //global intensity set to 4/16
    i2c_write_byte(port_num, AS1115_SLAVE_ADDR,0xb,0x3); //scan limit set to only display 4 digits 

}

/*
* writes to one of the 8 digit registers of the AS1115 with the desired value. 
* the device is configured to use BCD encoding, meaning values of 0-9 and -,E,H,L,P are 
* possible 
*/
void AS1115_display_write(int port_num, uint8_t slave_addr, uint8_t digit, uint8_t value) {

    i2c_write_byte(port_num, slave_addr, digit, value);

}

void display_hex_word(int port_num, uint8_t slave_addr, uint8_t digit0_val, uint8_t digit1_val, uint8_t digit2_val, uint8_t digit3_val) {

    AS1115_display_write(port_num, slave_addr,DIGIT_3,digit3_val);
    AS1115_display_write(port_num, slave_addr,DIGIT_2,digit2_val);
    AS1115_display_write(port_num, slave_addr,DIGIT_1,digit1_val);    
    AS1115_display_write(port_num, slave_addr,DIGIT_0,digit0_val);

}

void display_speed (int port_num, double speed) {

	uint8_t v_car_l = (uint32_t) speed % 10; 
	uint8_t v_car_h = ( (uint32_t) speed / 10) % 10; 

	AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_3,v_car_l);
	AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_2,v_car_h); 

    // i2c_write4_byte(port_num, AS1115_SLAVE_ADDR, DIGIT_0, 
    // 0, 
    // 0,
    // v_car_h,
    // v_car_l);    

}

void display_RPM (int port_num, double rpm) {

    uint8_t rpm_3 = (uint64_t) rpm % 10; 
    uint8_t rpm_2 = ( (uint64_t) rpm / 10) % 10;     
    uint8_t rpm_1 = ( (uint64_t) rpm / 100) % 10; 
    uint8_t rpm_0 = ( (uint64_t) rpm / 1000) % 10; 

    // AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_3,rpm_3);
    // AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_2,rpm_2);
    // AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_1,rpm_1);
    // AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_0,rpm_0);

    i2c_write4_byte(port_num, AS1115_SLAVE_ADDR, DIGIT_0, 
    rpm_0, 
    rpm_1,
    rpm_2,
    rpm_3);
    
}

void display_temp (int port_num, float temp) {
    uint8_t temp_l = (uint32_t) temp % 10; 
    uint8_t temp_h = ( (uint32_t) temp / 10) % 10; 
    uint8_t temp_hh = ( (uint32_t) temp / 100) % 10; 
    uint8_t temp_hhh = ( (uint32_t) temp / 1000) % 10; 
    AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_0,temp_l);
    AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_1,temp_h);     
    AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_2,temp_hh);
    AS1115_display_write(port_num, AS1115_SLAVE_ADDR,DIGIT_3,temp_hhh);
}

void display_disable(int port_num) {
    i2c_write_byte(port_num, AS1115_SLAVE_ADDR,0xc,0x80); //sets shutdown register for normal operation
}

#endif
