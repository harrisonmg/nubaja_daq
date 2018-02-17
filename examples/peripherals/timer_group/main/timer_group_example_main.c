#include "nubaja_esp32_helper.h" //all other includes located here

//TIMER CONFIGS
#define TIMER_DIVIDER               16  //  Hardware timer clock divider
#define TIMER_SCALE                 (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_FREQUENCY      1   // control loop period for timer group 0 timer 0 in seconds
#define PROGRAM_LENGTH              10 // program length for timer group 0 timer 1 in seconds

//ADC CONFIGS
#define V_REF               1000
#define V_FS                3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE           (V_FS/4096)
#define ATTENUATION         ADC_ATTEN_11db

//global vars
int level = 0;
SemaphoreHandle_t killSemaphore = NULL;
xQueueHandle ctrl_queue;
const char *TAG = "ESP3";
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
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function contains all functions to read data from any & all sensors
 */
void control() {
    ESP_LOGI(TAG, "ctrl");
    // gpio_set_level(GPIO_NUM_4, 1);
    read_adc(1,ADC1_CHANNEL_6);
    ERROR_HANDLE_ME(itg_read(0x0));
    // gpio_set_level(GPIO_NUM_4, 0);
}

/*
 * Resets interrupt and calls control function to interface sensors
 */
void control_thread_function() 
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
 * Ends the task passed in as an argument and then ends itself
 * Task blocks until semaphore is given from program timer 1 ISR
 */
void end_program(void* task) {   
    while(1) {
        if (xSemaphoreTake( killSemaphore, portMAX_DELAY ) == pdTRUE) //end program after dumping to file
        {
            ESP_LOGI(TAG, "end_program");
            vTaskPrioritySet((TaskHandle_t*) task,(configMAX_PRIORITIES-2));
            for (int n=0;n<5;n++) {
                vTaskSuspend((TaskHandle_t*) task);
                vTaskDelay(10);
            }
            vTaskDelay(500); 
            ERROR_HANDLE_ME(dump_to_file(f_buf,err_buf)); 
            ESP_LOGI(TAG, "suspending task");
            vTaskSuspend(NULL);
        }
    }
}

/*
 * configures all necessary modules using respective config functions
 */
void config() {
    //adc config
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);
    
    //SD config 
    sd_config(); 

    //GPIO config
    gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);

    //timer config
    /* Select and initialize basic parameters of the timer */
    //timer init args: timer index, auto reload, timer length
    timer_setup(0,1,CONTROL_LOOP_FREQUENCY); //control loop timer
    timer_setup(1,0,PROGRAM_LENGTH); //control loop timer 
    
    //semaphore that blocks end program task 
    killSemaphore = xSemaphoreCreateBinary();
    
    //i2c and IMU config
    i2c_master_config();
    // itg_3200_config();`
}

/*
* creates tasks
*/
void app_main() { 
    config(); 
    TaskHandle_t ctrlHandle = NULL;
    TaskHandle_t endHandle = NULL;
    ctrl_queue = xQueueCreate(10, sizeof(timer_event_t));
    xTaskCreate(control_thread_function, "control_thread_function", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
    xTaskCreate(end_program, "end_program", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle);
}