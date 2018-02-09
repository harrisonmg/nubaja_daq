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

//TIMER CONFIGS
#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_FREQUENCY   (0.5   )   // control loop period for timer group 0 timer 0 in seconds
#define PROGRAM_LENGTH 10 // program length for timer group 0 timer 1 in seconds

//ADC CONFIGS
#define V_REF   1000
#define V_FS 3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE (V_FS/4096)
#define ATTENUATION ADC_ATTEN_11db

//buffer config
#define SIZE 1000
#define HEX 16

//sad card spi config
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 15
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   13

//I2C CONFIG
#define I2C_EXAMPLE_MASTER_SCL_IO          22               /*!< gpio number for I2C master clock */
#define I2C_EXAMPLE_MASTER_SDA_IO          23               /*!< gpio number for I2C master data  */
#define I2C_NUM                            I2C_NUM_0        /*!< I2C port number for master dev */
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE  0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE  0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_FREQ_HZ         400000           /*!< I2C master clock frequency */
#define WRITE_BIT                          I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                           I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                       0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                      0x0              /*!< I2C master will not check ack from slave */
#define ACK                                0x0              /*!< I2C ack value */
#define NACK                               0x1              /*!< I2C nack value */
#define DATA_LENGTH                        1                //in bytes
#define I2C_TASK_LENGTH                    10              //in ms


//errors
#define SUCCESS 0
#define I2C_READ_FAILED 1
#define FILE_DUMP_ERROR 2

//global vars
int level = 0;
SemaphoreHandle_t killSemaphore = NULL;
xQueueHandle ctrl_queue;
static const char *TAG = "bois";
char f_buf[SIZE];
char err_buf[SIZE];

//interrupt flag container
typedef struct {
    int ctrl_intr;
} timer_event_t;

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

/*
 * configures one i2c module for operation as an i2c master with internal pullups disabled
 */
 void i2c_master_config() {
    ESP_LOGI(TAG, "i2c_master_config");
    int i2c_master_port = I2C_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_EXAMPLE_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.scl_io_num = I2C_EXAMPLE_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.master.clk_speed = I2C_EXAMPLE_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_EXAMPLE_MASTER_RX_BUF_DISABLE,
                       I2C_EXAMPLE_MASTER_TX_BUF_DISABLE, 0);
}

/*
 * mounts SD card
 * configures SPI bus for SD card comms
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

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

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
}

/*
 * configures all necessary modules using respective config functions
 */
void config() {
    //adc config
    // adc1_config_width(ADC_WIDTH_BIT_12);
    // adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);

    // //GPIO config
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);

    //timer config
    /* Select and initialize basic parameters of the timer */
    //timer init args: timer index, auto reload, timer length
    timer_setup(0,1,CONTROL_LOOP_FREQUENCY); //control loop timer
    timer_setup(1,0,PROGRAM_LENGTH); //control loop timer 

    killSemaphore = xSemaphoreCreateBinary();

    i2c_master_config();
}


/*
 * writes data buffer to a file on SD card
 */
int dump_to_file(char buffer[],char err_buffer[]) {
    char nullstr[] = "\0";
    strcpy(buffer,nullstr);
    FILE *fp;
    fp = fopen("/sdcard/data.txt", "a");
    if (fp == NULL)
    {
        return FILE_DUMP_ERROR;
    }   
    fputs(buffer, fp);
    fputs(err_buffer, fp);
    ESP_LOGI(TAG, "dumped");
    fclose(fp);
    esp_vfs_fat_sdmmc_unmount();
    return SUCCESS;
}

/*
 * appends integer to the end of the buffer
 */
void add_int_to_buffer (char buf[],int i_to_add) {
    char str_to_add [sizeof(int)*8+1];
    itoa(i_to_add,str_to_add,HEX);
    strcpy(buf,str_to_add);
}

//takes around 500us @100kHz
/*
 * reads a register from an I2C device
 * can be configured to read an 8bit or 16bit register 
 * automatically adds result to the buffer
 */
int itg_read(int reg) 
{
    // gpio_set_level(GPIO_NUM_4, 1);//start timer
    int ret;
    uint8_t* data_h = (uint8_t*) malloc(DATA_LENGTH);
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
    int data = (*data_h << 8 | *data_l); //comment out for one byte read
    add_int_to_buffer(f_buf,data);
    // gpio_set_level(GPIO_NUM_4, 0); //end timer 
    if (ret != ESP_OK) {
        printf("i2c read failed\n");
        return I2C_READ_FAILED; //dead sensor
    } else {
        return SUCCESS;
    }
}

void read_adc(int channel,...) 
{
    va_list valist;
    va_start(valist, channel);
    for (int i = 0; i < channel; i++) {
        int val_0 = adc1_get_raw(channel); //* ADC_SCALE
        add_int_to_buffer(f_buf,val_0);        
    }    

}

/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function contains all functions to read data from any & all sensors
 */
void control() {
    // gpio_set_level(GPIO_NUM_4, level);
    // level = !level;
    // int val_0 = adc1_get_raw(ADC1_CHANNEL_6); //* ADC_SCALE
    // add_int_to_buffer(f_buf,val_0);
    read_adc(ADC1_CHANNEL_6);
    ERROR_HANDLE_ME(itg_read(0x0));
}

/*
 * Resets interrupt and calls control function to interface sensors
 */
static void control_thread_function() 
{
    timer_event_t evt;
    while (1) 
    {
        if (xQueueReceive(ctrl_queue, &evt, 1/portTICK_PERIOD_MS)) //1 khz control loop operation
        { 
            evt.ctrl_intr = 0;
            control();
        }
    }
}

/*
 * Turns off all GPIO pins
 */
void gpio_kill(int pin)
{
    ESP_LOGI(TAG, "gpio_kill");
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_INPUT);  
}

/*
 * Ends the task passed in as an argument and then ends itself
 * Task blocks until semaphore is given from program timer 1 ISR
 */
static void end_program(void* task) {   
    while(1) {
        if (xSemaphoreTake( killSemaphore, portMAX_DELAY ) == pdTRUE) //end program after dumping to file
        {
            ESP_LOGI(TAG, "end_program");
            for (int n=0;n<5;n++) {
                vTaskSuspend((TaskHandle_t*) task);
            }
            vTaskDelay(500); //delay for .1s
            // // ERROR_HANDLE_ME(dump_to_file(f_buf,err_buf)); 
            // gpio_kill(GPIO_NUM_4); //disable GPIO
            ESP_LOGI(TAG, "suspending task");
            vTaskSuspend(NULL);
        }
    }
}

/*
* creates tasks
*/
void app_main() { 
    config();
    TaskHandle_t ctrlHandle = NULL;
    TaskHandle_t endHandle = NULL;
    ctrl_queue = xQueueCreate(10, sizeof(timer_event_t));
    xTaskCreatePinnedToCore(control_thread_function, "control_thread_function", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle,0);
    xTaskCreatePinnedToCore(end_program, "end_program", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle,1);
}

