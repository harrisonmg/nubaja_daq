#include "nubaja_esp32_helper.h" //all other includes located here

//global vars
int level = 0;
SemaphoreHandle_t killSemaphore = NULL;
xQueueHandle ctrl_queue;
const char *TAG = "ESP3";
char f_buf[SIZE];
char err_buf[SIZE];
int buffer_idx = 0;

/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function contains all functions to read data from any & all sensors
 */
void control() {
    ESP_LOGI(TAG, "ctrl");
    read_adc(1,ADC1_CHANNEL_6); //first argument is number of arguments
    ERROR_HANDLE_ME(itg_read(XH));
    ERROR_HANDLE_ME(itg_read(YH));
    ERROR_HANDLE_ME(itg_read(ZH));
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
            gpio_set_level(GPIO_NUM_4, 1);
            control();
            gpio_set_level(GPIO_NUM_4, 0);
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
            ERROR_HANDLE_ME(dump_to_file(f_buf,err_buf,1)); 
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
    itg_3200_config();
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