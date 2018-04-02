#ifndef LSM6DSM_DRIVER
#define LSM6DSM_DRIVER

/* 
* includes
*/ 

//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

//kernel
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

//esp
#include "esp_types.h"
#include "esp_system.h"
#include "sdmmc_cmd.h"
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "soc/timer_group_struct.h"
#include "esp_spi_flash.h"

#include "nubaja_i2c_driver.h"
#include "nubaja_logging.h"

// LSM6DSM register mappings 
#define OUTX_L_G                            0x22
#define OUTX_H_G                            0x23
#define OUTY_L_G                            0x24
#define OUTY_H_G                            0x25
#define OUTZ_L_G                            0x26
#define OUTZ_H_G                            0x27
#define OUTX_L_XL                           0x28
#define OUTX_H_XL                           0x29
#define OUTY_L_XL                           0x2a
#define OUTY_H_XL                           0x2b 
#define OUTZ_L_XL                           0x2c
#define OUTZ_H_XL                           0x2d
#define CTRL1_XL                            0x10
#define CTRL8_XL                            0x17
#define CTRL2_G                             0x11
#define IMU_SLAVE_ADDR 		            0x6a
#define IMU_GYRO_FS                         2000 // full scale: +/- 2000 degrees / sec
#define IMU_GYRO_SCALE                      (IMU_GYRO_FS / 32767)
#define IMU_XL_FS                           16 // full scale: +/- 16 g's
#define IMU_XL_SCALE                        (IMU_XL_FS / 32767)

//vars
static const char *LSM6DSM_DRIVER_TAG = "LSM6DSM_DRIVER";
//DATASHEET: http://www.st.com/content/ccc/resource/technical/document/datasheet/76/27/cf/88/c5/03/42/6b/DM00218116.pdf/files/DM00218116.pdf/jcr:content/translations/en.DM00218116.pdf
/*
 * configures IMU with the following settings: 
 * 
 */
void LSM6DSM_config() {

        ESP_LOGI(LSM6DSM_DRIVER_TAG,"configuring IMU");
        
        uint8_t ODR_XL = 0x90;
        uint8_t FS_XL = 0x04; 
        uint8_t LPF1_BW_SEL = 0b0;
        uint8_t BWO_XL = 0b0;
        uint8_t CTRL1_XL_CONFIG = ( ODR_XL | FS_XL | LPF1_BW_SEL | BWO_XL );

        uint8_t LPF2_XL_EN = 0b0;
        uint8_t HPCF_XL = 0b00; 
        uint8_t HP_REF_MODE = 0b0;
        uint8_t INPUT_COMPOSITE = 0b0;
        uint8_t HP_SLOPE_XL_EN = 0b0;
        uint8_t LOW_PASS_ON_6D = 0b0;
        uint8_t CTRL8_XL_CONFIG = ( LPF2_XL_EN | HPCF_XL | HP_REF_MODE | INPUT_COMPOSITE | HP_SLOPE_XL_EN | 0 | LOW_PASS_ON_6D );

        uint8_t ODR_G = 0x90;
        uint8_t FS_G = 0x04; 
        uint8_t FS_125 = 0b0;
        uint8_t CTRL2_G_CONFIG = ( ODR_G | FS_G | FS_125 | 0 );

        ERROR_HANDLE_ME(i2c_write_byte(PORT_0, IMU_SLAVE_ADDR,CTRL1_XL,CTRL1_XL_CONFIG));
        ERROR_HANDLE_ME(i2c_write_byte(PORT_0, IMU_SLAVE_ADDR,CTRL8_XL,CTRL8_XL_CONFIG));
        ERROR_HANDLE_ME(i2c_write_byte(PORT_0, IMU_SLAVE_ADDR,CTRL2_G,CTRL2_G_CONFIG));

}

void LSM6DSM_gyro_test(int port_num, uint8_t slave_address, int reg) {

    struct sensor_output_t LSM6DSM_output; 

    ERROR_HANDLE_ME(i2c_read_3_reg(port_num, slave_address, reg, &LSM6DSM_output)); //this function doesn't work properly with the IMU - order of high and low registers is wrong
    add_s_16b_to_buffer(f_buf,(LSM6DSM_output.reg_0 * IMU_GYRO_SCALE));
    add_s_16b_to_buffer(f_buf,(LSM6DSM_output.reg_1 * IMU_GYRO_SCALE));
    add_s_16b_to_buffer(f_buf,(LSM6DSM_output.reg_2 * IMU_GYRO_SCALE));

}

#endif