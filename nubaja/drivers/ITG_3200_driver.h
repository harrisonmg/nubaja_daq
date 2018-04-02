#ifndef ITG_3200_DRIVER
#define ITG_3200_DRIVER

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

// ITG-3200 register mappings for gyroscope
#define XH                                  0x1D
#define XL                                  0x1E
#define YH                                  0x1F
#define YL                                  0x20
#define ZH                                  0x21
#define ZL                                  0x22
#define GYRO_SLAVE_ADDR 					0x69
#define GYRO_FS                             2000 // full scale: +/- 2000 degrees / sec
#define GYRO_SCALE                          (GYRO_FS / 32767)

//vars
static const char *ITG_3200_DRIVER_TAG = "ITG_3200_DRIVER";
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern int err_buffer_idx;
//datasheet: https://www.invensense.com/products/motion-tracking/3-axis/itg-3200/
//register map: https://www.invensense.com/wp-content/uploads/2015/02/ITG-3200-Register-Map.pdf

/*
 * configures gyroscope with the following settings: 
 * FS_SEL = 0x3 (+/-2000 degrees per second)
 * DLPF_CFG = 0x1 (BW = 188Hz, internal sample rate = 1kHz)
 * 
 */
void itg_3200_config() {
    ESP_LOGI(ITG_3200_DRIVER_TAG,"ITG-3200 config");
    uint8_t SMPLRT_DIV_REG= 0x15;
    uint8_t DLPF_FS_REG = 0x16;
    uint8_t DLPF_CFG_0 = 0x1;
    uint8_t DLPF_CFG_1 = 0x0; 
    uint8_t DLPF_CFG_2 = 0x0; 
    uint8_t DLPF_FS_SEL_0 = 0x8; 
    uint8_t DLPF_FS_SEL_1 = 0x10; 

    uint8_t DLPF_CFG = (DLPF_CFG_2 | DLPF_CFG_1 | DLPF_CFG_0);
    uint8_t DLPF_FS_SEL = (DLPF_FS_SEL_1 | DLPF_FS_SEL_0);
    uint8_t DLPF = (DLPF_FS_SEL | DLPF_CFG);
    uint8_t SMPLRT_DIV = 0x9; // 100 hz

    // ERROR_HANDLE_ME(i2c_write_byte(0x0,0x0,0x69));//address rewrite
    ERROR_HANDLE_ME(i2c_write_byte(PORT_0, GYRO_SLAVE_ADDR,SMPLRT_DIV_REG,SMPLRT_DIV));
    ERROR_HANDLE_ME(i2c_write_byte(PORT_0, GYRO_SLAVE_ADDR,DLPF_FS_REG,DLPF));
}

void itg_3200_test(int port_num, uint8_t slave_address, int reg) {
    struct sensor_output_t itg_3200_output; 
    ERROR_HANDLE_ME(i2c_read_3_reg(port_num, slave_address, reg, &itg_3200_output));
    add_s_16b_to_buffer(f_buf,(itg_3200_output.reg_0 * GYRO_SCALE));
    add_s_16b_to_buffer(f_buf,(itg_3200_output.reg_1 * GYRO_SCALE));
    add_s_16b_to_buffer(f_buf,(itg_3200_output.reg_2 * GYRO_SCALE));
}

#endif