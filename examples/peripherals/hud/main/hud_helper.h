#ifndef HUD_HELPER
#define HUD_HELPER

/* 
* includes
*/ 

//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <inttypes.h>
uint64_t t; //for printing uint64_t

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

//GPIO config
#define GPIO_INPUT_0                        4 //wheel spd hall effect in
#define GPIO_INPUT_PIN_SEL                  (1ULL<<GPIO_INPUT_0)
#define ESP_INTR_FLAG_DEFAULT               0

//errors
#define SUCCESS                             0
#define I2C_READ_FAILED                     1
#define FILE_DUMP_ERROR                     2
#define FILE_CREATE_ERROR                   3

//ITG-3200 register mappings for gyroscope
#define XH                                  0x1D
#define XL                                  0x1E
#define YH                                  0x1F
#define YL                                  0x20
#define ZH                                  0x21
#define ZL                                  0x22

//ADC CONFIGS
#define V_REF               1000
#define V_FS                3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE           (V_FS / 4096)
#define ATTENUATION         ADC_ATTEN_11db

//THERMISTOR CONFIGS (y=mx + b, linear fit to Vout vs. temperature of thermistor circuit)
#define THERM_M 0.024                    
#define THERM_B -0.5371
//thermistor pn: NTCALUG02A103F800

//buffer config
#define SIZE     2000

//TIMER CONFIGS
#define TIMER_DIVIDER               16  //  Hardware timer clock divider
#define TIMER_SCALE                 (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_PERIOD         .01   // control loop period for timer group 0 timer 0 in secondss
#define PROGRAM_LENGTH              60 // program length for timer group 0 timer 1 in seconds

#define PI                          3.14159265358979323846

/*****************************************************/


/* 
* globals
*/ 

extern const char *TAG;
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern xQueueHandle timer_queue;
extern xQueueHandle gpio_queue;
extern SemaphoreHandle_t killSemaphore;


//interrupt flag container
typedef struct {
    uint64_t timer_counts;
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
        case 3: //file dump error
            strcpy(msg, "file create error\n");
            record_error(err_buf,msg);
            break;             
        default: 
            NULL;
    }
}

void print_timer_counter(uint64_t counter_value)
{
    printf("Counter: 0x%08x%08x\n", (uint32_t) (counter_value >> 32),
                                    (uint32_t) (counter_value));
    printf("Time   : %.8f s\n", (double) counter_value / TIMER_SCALE);
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
int i2c_read_2_byte(int reg) 
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
    i2c_master_read_byte(cmd, data_h, ACK);    
    i2c_master_read_byte(cmd, data_l, NACK);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); 
    i2c_cmd_link_delete(cmd);  
    uint16_t data = (*data_h << 8 | *data_l); //comment out for one byte read
    //uint16_t data = *data_l; //uncomment for one byte read
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG,"i2c read failed");
        return I2C_READ_FAILED; //dead sensor
        free(data_h); //comment out for one byte read
        free(data_l);
        vTaskSuspend(NULL);
    } else {
        free(data_h); //comment out for one byte read
        free(data_l);
        return SUCCESS;
    }
}

/*
* function designed with variable number of arguments
* Turns off all GPIO pins passed in as arguments
*/
void gpio_kill(int num,...)
{
    va_list valist;
    va_start(valist, num);

    for (int i=0;i<num;i++) {
        gpio_set_level(va_arg(valist, int), 0);
        gpio_set_direction(va_arg(valist, int), GPIO_MODE_INPUT);          
    }
    va_end(valist);   
}

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
        // add_12b_to_buffer(f_buf,val_0);        
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

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_queue, &gpio_num, NULL);
}

void config_gpio() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE; //interrupt of rising edge
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL; //bit mask of the pins
    io_conf.mode = GPIO_MODE_INPUT;//set as input mode    
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);


    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT); //install gpio isr service
    gpio_isr_handler_add(GPIO_INPUT_0, gpio_isr_handler, (void*) GPIO_INPUT_0); //hook isr handler for specific gpio pin
}

/*
 * Timer group0 ISR handler
 * sets ctrl_intr flag high each time alarm occurs, re-enables alarm and sends data to main program task
 * also unblocks end_program task at appropriate time decided by PROGRAM_LENGTH
 */
void IRAM_ATTR timer_group0_isr(void *para) {
    timer_event_t evt;
    uint32_t intr_status = TIMERG0.int_st_timers.val;

    TIMERG0.hw_timer[1].update = 1;
    uint64_t timer_counter_value = ((uint64_t) TIMERG0.hw_timer[1].cnt_high) 
                                   << 32 | TIMERG0.hw_timer[1].cnt_low;
    
    if ((intr_status & BIT(0))) { //if alarm is true, set interrupt flag 
        TIMERG0.int_clr_timers.t0 = 1; //clear timer interrupt bit
        evt.timer_counts = timer_counter_value; 
    } 
    if ((intr_status & BIT(1))) { //if alarm is true, set interrupt flag 
        TIMERG0.int_clr_timers.t1 = 1; //clear timer interrupt bit
        xSemaphoreGiveFromISR(killSemaphore,NULL); //GIVE SEMAPHORE
    }
    TIMERG0.hw_timer[0].config.alarm_en = TIMER_ALARM_EN; //re-enable timer for timer 0 which is timing control loop

    // send the event data back to the main program task
    xQueueSendFromISR(timer_queue, &evt, NULL);
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