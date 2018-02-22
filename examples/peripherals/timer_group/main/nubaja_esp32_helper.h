#ifndef NUBAJA_ESP32_HELPER
#define NUBAJA_ESP32_HELPER

/* 
* includes
*/ 

//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

//kernel
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

//esp
#include "esp_types.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "soc/timer_group_struct.h"

/* 
* defines
*/ 

//I2C CONFIG
#define I2C_MASTER_SCL_IO                   22               /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO                   23               /*!< gpio number for I2C master data  */
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

//errors
#define SUCCESS                             0
#define I2C_READ_FAILED                     1
#define FILE_DUMP_ERROR                     2

//sad card spi config
#define PIN_NUM_MISO                        18
#define PIN_NUM_MOSI                        19
#define PIN_NUM_CLK                         14
#define PIN_NUM_CS                          15

//ITG-3200 register mappings for gyroscope
#define XH                                  0x1D
#define XL                                  0x1E
#define YH                                  0x1F
#define YL                                  0x20
#define ZH                                  0x21
#define ZL                                  0x22

//buffer config
#define SIZE                                1000

//TIMER CONFIGS
#define TIMER_DIVIDER               16  //  Hardware timer clock divider
#define TIMER_SCALE                 (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_PERIOD         .01   // control loop period for timer group 0 timer 0 in seconds
#define PROGRAM_LENGTH              180 // program length for timer group 0 timer 1 in seconds

//ADC CONFIGS
#define V_REF               1000
#define V_FS                3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE           (V_FS/4096)
#define ATTENUATION         ADC_ATTEN_11db


/*****************************************************/


/* 
* globals
*/ 

extern const char *TAG;
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern xQueueHandle ctrl_queue;
extern SemaphoreHandle_t killSemaphore;


//interrupt flag container
typedef struct {
    int ctrl_intr;
} timer_event_t;

/*****************************************************/



/* HELPER FUNCTIONS
* these functions do not interface with sensors
* but rather they perform certain operations 
* necessary to streamline and simplify operations 
*
*/

void record_error(char err_buffer[], char err_msg[]) {
    strcpy(err_buffer,err_msg);
}

void ERROR_HANDLE_ME(int err_num) {
    char msg[50];
    switch (err_num) {
        case 0: //no error
            break;  
        case 1: //i2c read error
            strcpy(msg,"i2c read error\n");
            record_error(err_buf,msg);
            break;    
        case 2: //file dump error
            strcpy(msg, "file dump error\n");
            record_error(err_buf,msg);
            break; 
        default: 
            NULL;
    }
}

/*
 * writes data buffer to a file on SD card
 */
int dump_to_file(char buffer[],char err_buffer[],bool unmount) {
    FILE *fp;
    fp = fopen("/sdcard/DAQME.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
        return FILE_DUMP_ERROR;
    }   
    fputs(buffer, fp);
    // printf("final buffer: %s\n",buffer);
    // fputs(err_buffer, fp);
    // printf("err buffer: %s\n",err_buffer);
        
    fclose(fp);
    if (unmount) {
        esp_vfs_fat_sdmmc_unmount();
        ESP_LOGI(TAG, "umounted & buffer dumped");
        return SUCCESS;
    }

    ESP_LOGI(TAG, "buffer dumped");
    return SUCCESS;
}


/*
 * appends 16b integer to the end of the buffer
 * adds 2 hex digits to the end of the buffer
 * designed for use with I2C reads of the itg-3200
 * which has 16b registers
 */
void add_16b_to_buffer (char buf[],uint16_t i_to_add) {
    char formatted_string [17]; //number of bits + 1
    sprintf(formatted_string,"%04x",i_to_add);
    strcat(buf,formatted_string);
    buffer_idx+=4;
    if (buffer_idx >= SIZE) {
       buffer_idx = 0;
       ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
       memset(buf,0,strlen(buf));
    }    
}

/*
 * appends 32b integer to the end of the buffer
 * adds 3 hex digits to the end of the buffer
 * designed with use case of adc read in mind (12b resolution)
 */
void add_12b_to_buffer (char buf[],uint16_t i_to_add) {
    char formatted_string [13]; //number of bits + 1
    sprintf(formatted_string,"%03x",i_to_add);
    strcat(buf,formatted_string);
    buffer_idx+=3;
    if (buffer_idx >= SIZE) {
        buffer_idx = 0;
        ERROR_HANDLE_ME(dump_to_file(buf,err_buf,0)); 
        memset(buf,0,strlen(buf)); 
    }   
}



/*****************************************************/

/* SENSOR INTERFACE FUNCTIONS
* these functions interface with sensors  
* in order to read and record data 
*  
*
*/

/*
 * writes a single byte of data to a particular register using I2C protocol 
 */
int itg_3200_write(uint8_t slave_address, uint8_t reg, uint8_t data) {
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
        ESP_LOGE(TAG,"i2c write failed");
        return I2C_READ_FAILED; //dead sensor
    } else { 
        return SUCCESS;
    }
}   



//takes around 500us @100kHz
/*
 * reads a register from an I2C device
 * can be configured to read an 8bit or 16bit register 
 * automatically adds result to the buffer
 */
int itg_read(int reg) 
{
    int ret;
    uint8_t* data_h = (uint8_t*) malloc(DATA_LENGTH); //comment out for one byte read
    uint8_t* data_l = (uint8_t*) malloc(DATA_LENGTH);
    uint8_t gyro_slave_address = 0x69; 

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( gyro_slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, reg, ACK); 
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( gyro_slave_address << 1 ) | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, data_h, ACK); //comment out for one byte read
    i2c_master_read_byte(cmd, data_l, NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    uint16_t data = (*data_h << 8 | *data_l); //comment out for one byte read
    // uint16_t data = *data_l;
    add_16b_to_buffer(f_buf,data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"i2c read failed");
        return I2C_READ_FAILED; //dead sensor
        free(data_h);
        free(data_l);
        vTaskSuspend(NULL);
    } else {
        free(data_h);
        free(data_l);
        return SUCCESS;
    }
}

/*
* function designed with variable number of arguments
* Turns off all GPIO pins passed in as arguments
*/
// void gpio_kill(int num,...)
// {
//     va_list valist;
//     va_start(valist, num);

//     for (int i=0;i<num;i++) {
//         gpio_set_level(va_arg(valist, int), 0);
//         gpio_set_direction(va_arg(valist, int), GPIO_MODE_INPUT);          
//     }
//     va_end(valist);   
// }

/*
* function designed with variable number of arguments
* allows for adc reads of multiple channels without
* several repetitive function calls
* function reads a single channel from the adc
* the raw value is from 0-4095 (12b resolution)
* the raw value is then added to the buffer appropriately
*/
void read_adc(int num,...) 
{  
    va_list valist;
    uint16_t val_0;

    /* initialize valist for num number of arguments */
    va_start(valist, num);

    /* access all the arguments assigned to valist */
    for (int i = 0; i < num; i++) {
        val_0 = adc1_get_raw(va_arg(valist, int));
        add_12b_to_buffer(f_buf,val_0);        
    }

    /* clean memory reserved for valist */
    va_end(valist);    
}


/*****************************************************/


/* CONFIGURATION (SENSOR OR MODULE) FUNCTIONS
* these functions take care of the configuration of either
* ESP32 hardware modules or sensors the ESP32 is interfacing with
* for example the I2C modules and subsequent I2C slave devices 
*
*/

/*
 * configures gyroscope with the following settings: 
 * FS_SEL = 0x3 (+/-2000 degrees per second)
 * DLPF_CFG = 0x1 (BW = 188Hz, internal sample rate = 1kHz)
 * 
 */
void itg_3200_config() {
    uint8_t SMPLRT_DIV_REG= 0x15;
    uint8_t DLPF_FS_REG = 0x16;
    uint8_t DLPF_CFG_0 = 0x1;
    uint8_t DLPF_CFG_1 = 0x0; 
    uint8_t DLPF_CFG_2 = 0x0; 
    uint8_t DLPF_FS_SEL_0 = 0x8; 
    uint8_t DLPF_FS_SEL_1 = 0x10; 
    uint8_t gyro_slave_address = 0x69; 

    uint8_t DLPF_CFG = (DLPF_CFG_2 | DLPF_CFG_1 | DLPF_CFG_0);
    uint8_t DLPF_FS_SEL = (DLPF_FS_SEL_1 | DLPF_FS_SEL_0);
    uint8_t DLPF = (DLPF_FS_SEL | DLPF_CFG);
    uint8_t SMPLRT_DIV = 0x9; //100 hz

    // # Configure the gyroscope
    // # Set the gyroscope scale for the outputs to +/-2000 degrees per second
    // itg.write8(DLPF_FS, (DLPF_FS_SEL_0|DLPF_FS_SEL_1|DLPF_CFG_0))
    // itg.write8(SMPLRT_DIV, 9)
    ERROR_HANDLE_ME(itg_3200_write(gyro_slave_address,SMPLRT_DIV_REG,SMPLRT_DIV));
    ERROR_HANDLE_ME(itg_3200_write(gyro_slave_address,DLPF_FS_REG,DLPF));
}

/*
 * configures one i2c module for operation as an i2c master with internal pullups disabled
 */
 void i2c_master_config() {
    // ESP_LOGI(TAG, "i2c_master_config");
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
 * mounts SD card
 * configures SPI bus for SD card comms
 * SPI lines need 10k pull-ups 
 */
void sd_config() 
{
    ESP_LOGI(TAG, "sd_config");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = PIN_NUM_MISO;
    slot_config.gpio_mosi = PIN_NUM_MOSI;
    slot_config.gpio_sck  = PIN_NUM_CLK;
    slot_config.gpio_cs   = PIN_NUM_CS;    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };

    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    // sdmmc_card_print_info(stdout, card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem; suspending task");
            vTaskSuspend(NULL);
            // ESP_LOGE(TAG, "Failed to mount filesystem. "
            //     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card");
            vTaskSuspend(NULL);
        }
        return;
    }
    // esp_vfs_fat_sdmmc_unmount();
}

/*
 * Timer group0 ISR handler
 * sets ctrl_intr flag high each time alarm occurs, re-enables alarm and sends data to main program task
 * also unblocks end_program task at appropriate time decided by PROGRAM_LENGTH
 */
void IRAM_ATTR timer_group0_isr(void *para) {
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    timer_event_t evt;
    if ((intr_status & BIT(0))) { //if alarm is true, set interrupt flag 
        TIMERG0.int_clr_timers.t0 = 1; //clear timer interrupt bit
        evt.ctrl_intr = 1; //set flag
    } 
    if ((intr_status & BIT(1))) { //if alarm is true, set interrupt flag 
        TIMERG0.int_clr_timers.t1 = 1; //clear timer interrupt bit
        xSemaphoreGiveFromISR(killSemaphore,NULL); //GIVE SEMAPHORE
    }
    TIMERG0.hw_timer[0].config.alarm_en = TIMER_ALARM_EN; //re-enable timer for timer 0 which is timing control loop

    // send the event data back to the main program task
    xQueueSendFromISR(ctrl_queue, &evt, NULL);
}


/*
 * sets up timer group 0 timers 0 and 1
 * timer 0 times the control loop, set up for auto reload upon alarm
 * timer 1 times the entire program, does not reload on alarm
 */
void timer_setup(int timer_idx,bool auto_reload, double timer_interval_sec)
{
    /* Select and initialize basic parameters of the timer */
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = auto_reload;
    timer_init(TIMER_GROUP_0, timer_idx, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x0);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_sec * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr, 
        (void *) timer_idx, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, timer_idx);
}


/*****************************************************/

#endif