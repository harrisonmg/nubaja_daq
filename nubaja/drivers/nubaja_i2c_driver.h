#ifndef NUBAJA_I2C_DRIVER
#define NUBAJA_I2C_DRIVER

//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>

//kernel
#include "freertos/FreeRTOS.h"

//esp
#include "esp_types.h"
#include "esp_system.h"
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"

//custom
#include "nubaja_logging.h"

//I2C MODULE
#define I2C_MASTER_SDA_IO                   23               /*!< gpio number for I2C master data  */
#define I2C_MASTER_SCL_IO                   22               /*!< gpio number for I2C master clock */
#define I2C_NUM                             I2C_NUM_0        /*!< I2C port number for master dev */
#define I2C_MASTER_TX_BUF_DISABLE           0                /*!< I2C master do not need buffer */
#define I2C_MASTER_RX_BUF_DISABLE           0                /*!< I2C master do not need buffer */
#define I2C_CLK_HZ                          400000           /*!< I2C master clock frequency */
#define WRITE_BIT                           I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                            I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                        0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                       0x0              /*!< I2C master will not check ack from slave */
#define ACK                                 0x0              /*!< I2C ack value */
#define NACK                                0x1              /*!< I2C nack value */
#define DATA_LENGTH                         1                //in bytes
#define I2C_TASK_LENGTH                     1              //in ms

//vars
static const char *NUBAJA_I2C_DRIVER_TAG = "NUBAJA_I2C_DRIVER";
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern int err_buffer_idx;

/*
 * configures one i2c module for operation as an i2c master with internal pullups disabled
 */
 void i2c_master_config() {
    int i2c_master_port = I2C_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.master.clk_speed = I2C_CLK_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_MASTER_RX_BUF_DISABLE,
                       I2C_MASTER_TX_BUF_DISABLE, 0);
}

/*
 * writes a single byte of data to a register using I2C protocol 
 */
int i2c_write_byte(uint8_t slave_address, uint8_t reg, uint8_t data) {
    int ret; 
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_write_byte(cmd, data, ACK); 
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    if (ret != ESP_OK) {
        ESP_LOGE(NUBAJA_I2C_DRIVER_TAG,"i2c write failed");
        return I2C_READ_FAILED; //dead sensor
    } else { 
        return SUCCESS;
    }
}   

int i2c_write_byte_dis(uint8_t slave_address, uint8_t reg, uint8_t data) {
    int ret; 
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | WRITE_BIT, ACK_CHECK_DIS);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_write_byte(cmd, data, ACK); 
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    if (ret != ESP_OK) {
        ESP_LOGE(NUBAJA_I2C_DRIVER_TAG,"i2c write failed");
        return I2C_READ_FAILED; //dead sensor
    } else { 
        return SUCCESS;
    }
} 

/*
 * reads a register from an I2C device
 * can be configured to read an 8bit or 16bit register 
 * automatically adds result to the data buffer
 */
int i2c_read_2_byte(uint8_t slave_address, int reg) 
{
    int ret;
    uint8_t* data_h = (uint8_t*) malloc(DATA_LENGTH); //comment out for one byte read
    uint8_t* data_l = (uint8_t*) malloc(DATA_LENGTH);

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data_h, ACK);    
    i2c_master_read_byte(cmd, data_l, NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    uint16_t data = (*data_h << 8 | *data_l); //comment out for one byte read
    //uint16_t data = *data_l; //uncomment for one byte read
    add_16b_to_buffer (f_buf, data);

    if (ret != ESP_OK) {
        ESP_LOGE(NUBAJA_I2C_DRIVER_TAG,"i2c read failed");
        return I2C_READ_FAILED; //dead sensor
        free(data_h); //comment out for one byte read
        free(data_l);
        // vTaskSuspend(NULL);
    } else {
        free(data_h); //comment out for one byte read
        free(data_l);
        return SUCCESS;
    }
}

/*
* reads 3 registers as a burst read
* should be faster than calling itg_read 3 times sequentially
*/
int i2c_read_3_reg(uint8_t slave_address, int reg) 
{
    int ret;
    uint8_t* data_h_0 = (uint8_t*) malloc(DATA_LENGTH); 
    uint8_t* data_l_0 = (uint8_t*) malloc(DATA_LENGTH);    
    uint8_t* data_h_1 = (uint8_t*) malloc(DATA_LENGTH); 
    uint8_t* data_l_1 = (uint8_t*) malloc(DATA_LENGTH);    
    uint8_t* data_h_2 = (uint8_t*) malloc(DATA_LENGTH); 
    uint8_t* data_l_2 = (uint8_t*) malloc(DATA_LENGTH);
    

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( slave_address << 1 ) | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data_h_0, ACK);    
    i2c_master_read_byte(cmd, data_h_0, ACK); 
    
    i2c_master_read_byte(cmd, data_h_1, ACK); 
    i2c_master_read_byte(cmd, data_h_1, ACK); 

    i2c_master_read_byte(cmd, data_h_2, ACK); 
    i2c_master_read_byte(cmd, data_l_2, NACK);
    
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd); 

    if (ret != ESP_OK) {
        ESP_LOGE(NUBAJA_I2C_DRIVER_TAG,"i2c read failed");
        free(data_h_0); 
        free(data_l_0);
        free(data_h_1); 
        free(data_l_1);
        free(data_h_2); 
        free(data_l_2);   
        return I2C_READ_FAILED; // dead sensor            
    } else {
        uint16_t data_0 = (*data_h_0 << 8 | *data_l_0); 
        uint16_t data_1 = (*data_h_1 << 8 | *data_l_1); 
        uint16_t data_2 = (*data_h_2 << 8 | *data_l_2); 
        
        add_16b_to_buffer(f_buf,data_0);
        add_16b_to_buffer(f_buf,data_1);
        add_16b_to_buffer(f_buf,data_2);    
        buffer_newline(f_buf); 
           
        free(data_h_0); 
        free(data_l_0);
        free(data_h_1); 
        free(data_l_1);
        free(data_h_2); 
        free(data_l_2);   

        return SUCCESS;
    }
}

#endif