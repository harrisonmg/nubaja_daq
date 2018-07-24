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

#include "esp_system.h"
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
#include "../../drivers/nubaja_logging.h"
/* 
* defines
*/ 

//GPIO 
#define HALL_EFF_GPIO                       26 //wheel spd hall effect in
#define ENGINE_RPM_GPIO                     27 //engine RPM measurement circuit. currently unrouted. 
#define CLK_GPIO                            33 //connected to 1kHz oscillator
#define START_STOP_GPIO                     36 //CONFIRM I CAN USE THIS
// #define GPIO_INPUT_PIN_SEL                  ( (1ULL<<HALL_EFF_GPIO) | (1ULL<<ENGINE_RPM_GPIO) | (1ULL<<CLK_GPIO) | (1ULL<<START_STOP_GPIO) )               
#define GPIO_INPUT_PIN_SEL                  ( (1ULL<<HALL_EFF_GPIO) | (1ULL<<ENGINE_RPM_GPIO) | (1ULL<<START_STOP_GPIO) )               
#define ESP_INTR_FLAG_DEFAULT               0
#define MPH_SCALE                           3.927 // TIRE DIAMETER (22") * PI * 3600 / 63360                                            
#define RPM_SCALE                           60 //RPM = 60 / period
#define FLASHER_GPIO                        32
#define H                                   1
#define L                                   0

//TIMER CONFIGS
#define CONTROL_LOOP_PERIOD                 .001   // control loop period
#define TIMER_DIVIDER                       16  //  Hardware timer clock divider
#define TIMER_SCALE                         (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds

  


/*****************************************************/


/* 
* globals
*/ 

extern char f_buf[];
extern char err_buf[];
extern int buffer_idx;
extern int err_buffer_idx;
extern xQueueHandle timer_queue;
extern SemaphoreHandle_t killSemaphore;
extern SemaphoreHandle_t commsSemaphore;
extern int program_len;
extern char *DHCP_IP;
extern int MPH_FLAG;
extern int RPM_FLAG; 
extern int CLK;
extern int START_STOP;

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
 * turns on/off flasher to indicate data is being recorded or not
 */
void flasher(int level) {
    
    gpio_set_level(FLASHER_GPIO,level); //activate relay G6L-1F DC3

}

/*
 * sets flasher GPIO as output
 */
void flasher_init(int flasher_gpio_num) {

    gpio_set_direction(flasher_gpio_num, GPIO_MODE_OUTPUT);

}

/*
* ISRs for GPIO based interrupts. interrupts are configured via config_gpio
*/
static void mph_isr_handler(void* arg) {

    MPH_FLAG = 1; 

}

static void rpm_isr_handler(void* arg) {

    RPM_FLAG = 1;

}

// static void clk_isr_handler(void* arg) {

//     CLK = 1;

// }

static void start_stop_isr_handler(void* arg) {

    //do something...
    START_STOP = !START_STOP;

}

/*
* configures GPIO pins for interrupt on rising edge
*/
void config_gpio() {
    
    //config rising-edge interrupt GPIO pins (hall eff, engine rpm, and clk)
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE; //interrupt of rising edge
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL; //bit mask of the pins
    io_conf.mode = GPIO_MODE_INPUT;//set as input mode    
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    gpio_install_isr_service(0); //install gpio isr service
    gpio_isr_handler_add(HALL_EFF_GPIO, mph_isr_handler, (void*) HALL_EFF_GPIO); //hook isr handler for gpio pins
    gpio_isr_handler_add(ENGINE_RPM_GPIO, rpm_isr_handler, (void*) ENGINE_RPM_GPIO); 
    // gpio_isr_handler_add(CLK_GPIO, clk_isr_handler, (void*) CLK_GPIO); 
    gpio_isr_handler_add(START_STOP_GPIO, start_stop_isr_handler, (void*) START_STOP_GPIO); 
    
}

/*
 * Timer group0 ISR handler
 * sets ctrl_intr flag high each time alarm occurs, re-enables alarm and sends data to main program task
 * also unblocks end_program task at appropriate time decided by PROGRAM_LENGTH
 */
void IRAM_ATTR timer_group0_isr(void *para) 
{
    
    uint32_t intr_status = TIMERG0.int_st_timers.val;

    TIMERG0.hw_timer[1].update = 1;
    
    if ((intr_status & BIT(0))) 
    {
        TIMERG0.int_clr_timers.t0 = 1; //clear timer interrupt bit
        CLK = 1; //raise flag
    } 

    TIMERG0.hw_timer[0].config.alarm_en = TIMER_ALARM_EN; //re-enable timer for timer 0 which is timing control loop

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


