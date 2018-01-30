#include <stdio.h>
#include <stdlib.h>
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "driver/gpio.h"
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
// #include <errno.h>
#include <string.h>

//TIMER CONFIGS
#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
// #define TIMER_IDX   0        
// #define TIMER_AUTO_RELOAD 1
#define CONTROL_LOOP_FREQUENCY   (.001)   // control loop period for timer group 0 timer 0 in seconds
#define PROGRAM_LENGTH 10 // program length for timer group 0 timer 1 in seconds

//ADC CONFIGS
#define V_REF   1000
#define V_FS 3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE (V_FS/4096)
#define ATTENUATION ADC_ATTEN_11db

//buffer config
#define SIZE 1000
#define HEX 16
char f_buf[SIZE];

//global vars
int level = 0;
int tic = 0;
SemaphoreHandle_t killSemaphore = NULL;
// extern int errno; 

//interrupt flag container
typedef struct {
    int ctrl_intr;
} timer_event_t;

xQueueHandle ctrl_queue;

/*
 * Timer group0 ISR handler
 * sets ctrl_intr flag high each time alarm occurs, re-enables alarm and sends data to main program task
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

static void timer_setup(int timer_idx,bool auto_reload, double timer_interval_sec)
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

//configures necessary modules for proper operation
void config() {
    //adc config
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);

    //GPIO config
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);

    //timer config
    /* Select and initialize basic parameters of the timer */
    //timer init args: timer index, auto reload, timer length
    timer_setup(0,1,CONTROL_LOOP_FREQUENCY); //control loop timer
    timer_setup(1,0,PROGRAM_LENGTH); //control loop timer 

    killSemaphore = xSemaphoreCreateBinary();
}


/*
* THIS SHIT BROKE
*/
void dump_to_file(char buf[]) {
    char nullstr[] = "\0";
    strcpy(buf,nullstr);
    FILE *fp;
    fp = fopen("test.txt", "a");
    if (fp == NULL)
    {
        printf("Error opening file!\n");
        vTaskSuspend(NULL);
    }   
    fputs(buf, fp); // this is preferable
    // fprintf(fp, "%s", buf);
    printf("dumped\n");
    fclose(fp);
    // return(1); //success
}

void add_int_to_buffer (char buf[],int i_to_add) {
    char str_to_add [sizeof(int)*8+1];
    itoa(i_to_add,str_to_add,HEX);
    printf("%s\n",str_to_add);
    strcpy(buf,str_to_add);
}

//function executed each time ctrl_intr is set 
void control() {
    gpio_set_level(GPIO_NUM_4, level);
    level = !level;
    int val_0 = adc1_get_raw(ADC1_CHANNEL_6); //* ADC_SCALE
    add_int_to_buffer(f_buf,val_0);
    // printf("%03x\n",val_0);

}

/*
 * The main task of this example program
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


void gpio_kill(int pin)
{
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_INPUT);  
}

//blocks until semaphore is given from program timer ISR
static void end_program(void* task) {
    // (TaskHandle_t*) task;
    if (xSemaphoreTake( killSemaphore, portMAX_DELAY ) == pdTRUE)
    {
        vTaskSuspend((TaskHandle_t*) task);
        vTaskDelay(100); //delay for .1s
        //end program after dumping to file
        // dump_to_file(f_buf); 
        
        gpio_kill(GPIO_NUM_4); //disable GPIO
        
        printf("reset me bb\n");
        
        vTaskSuspend(NULL);
    }
}

void app_main() { 
    config();
    TaskHandle_t ctrlHandle = NULL;
    ctrl_queue = xQueueCreate(10, sizeof(timer_event_t));
    xTaskCreate(control_thread_function, "control_thread_function", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
    xTaskCreate(end_program, "end_program", 2048, ctrlHandle, (configMAX_PRIORITIES-2),NULL);
}

