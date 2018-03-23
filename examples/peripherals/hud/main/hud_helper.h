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
#include <errno.h>

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
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_task_wdt.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "soc/timer_group_struct.h"
#include "esp_spi_flash.h"

//custom
#include </home/sparky/esp/esp-idf/examples/peripherals/nubaja/nubaja_logging.h>

/* 
* defines
*/ 

//GPIO 
#define HALL_EFF_GPIO                       4 //wheel spd hall effect in
#define ENGINE_RPM_GPIO                     12 //engine RPM measurement circuit
#define GPIO_INPUT_PIN_SEL                  ((1ULL<<HALL_EFF_GPIO) | (1ULL<<ENGINE_RPM_GPIO))
#define ESP_INTR_FLAG_DEFAULT               0
#define MPH_SCALE                           4.10 // TIRE DIAMETER (23") * PI * 3600 / 63360                                            
#define RPM_SCALE                           60 //RPM = 60 / period
#define FLASHER_GPIO                        25
//ADC 
#define V_REF                               1000
#define V_FS                                3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE                           (V_FS / 4096)
#define ATTENUATION                         ADC_ATTEN_11db

//THERMISTOR CONFIGS 
#define THERM_M                             0.024                    
#define THERM_B                             -0.5371 //(y=mx + b, linear fit to Vout vs. temperature of thermistor circuit)
//thermistor pn: NTCALUG02A103F800

//TIMER CONFIGS
#define TIMER_DIVIDER                       16  //  Hardware timer clock divider
#define TIMER_SCALE                         (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_PERIOD                 .01   // control loop period for timer group 0 timer 0 in secondss
#define PROGRAM_LENGTH                      60 // program length for timer group 0 timer 1 in seconds

  


/*****************************************************/


/* 
* globals
*/ 
static const char *TAG = "HUD_HELPER";
extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern int err_buffer_idx;
extern xQueueHandle timer_queue;
extern xQueueHandle gpio_queue;
extern SemaphoreHandle_t killSemaphore;
extern SemaphoreHandle_t commsSemaphore;
extern int program_len;
extern char *DHCP_IP;


//interrupt flag container
typedef struct {
    uint64_t timer_counts;
} timer_event_t;

/*****************************************************/

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
* the raw value is then added to the data buffer appropriately
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

/*
* ISR for GPIO based interrupt. interrupt is configured via config_gpio
*/
static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_queue, &gpio_num, NULL);
}

/*
* configures a GPIO pin for an interrupt on a rising edge
*/
void config_gpio() {
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE; //interrupt of rising edge
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL; //bit mask of the pins
    io_conf.mode = GPIO_MODE_INPUT;//set as input mode    
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT); //install gpio isr service
    gpio_isr_handler_add(HALL_EFF_GPIO, gpio_isr_handler, (void*) HALL_EFF_GPIO); //hook isr handler for gpio pins
    gpio_isr_handler_add(ENGINE_RPM_GPIO, gpio_isr_handler, (void*) ENGINE_RPM_GPIO); 
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
 * timer 1 also used to calculate vehicle speed via measurement of time between GPIO interrupts 
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


