#include "hud_helper.h" //all other includes located here
#define SENSOR_ENABLE 1 //0 or 1


//global vars
int level = 0;
SemaphoreHandle_t killSemaphore = NULL;
xQueueHandle timer_queue = NULL;
xQueueHandle gpio_queue = NULL;
const char *TAG = "ESP_HUD";
char f_buf[SIZE];
char err_buf[SIZE];
int buffer_idx = 0;



/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function contains all functions to read data from any & all sensors
 */
void control(timer_event_t evt) {
    // ESP_LOGI(TAG, "ctrl");
    if(SENSOR_ENABLE == 1) {
        uint32_t gpio_num;
        if ((xQueueReceive(gpio_queue, &gpio_num, 0)) == pdTRUE) {
            //get current time
            //subtract current time from old time
            //convert period (seconds/rev) to vehicle speed (mph) and write to screen
            //old time = current time
            //read ADC (thermistor)
            //convert ADC counts to temperature
            //also write temperature to screen
        }
    }
}

/*
 * Resets interrupt and calls control function to interface sensors
 */
void control_thread_function() 
{
    timer_event_t evt;
    while (1) 
    {
        if ((xQueueReceive(timer_queue, &evt, 0)) == pdTRUE) //0 or port max delay? 
        { 
            control(evt);
        }
    }
}

/*
 * Ends the task passed in as an argument and then ends itself
 * Task blocks until semaphore is given from program timer 1 ISR
 */
void end_program(void* task) {   
    while(1) {
        if (xSemaphoreTake(killSemaphore, portMAX_DELAY) == pdTRUE) //end program after dumping to file
        {
            ESP_LOGI(TAG, "end_program");
            vTaskPrioritySet((TaskHandle_t*) task,(configMAX_PRIORITIES-2));
            for (int n=0;n<10;n++) {
                vTaskSuspend((TaskHandle_t*) task);
                vTaskDelay(1);
            }
            ESP_LOGI(TAG, "goodbye!");
            vTaskSuspend(NULL);
        }
    }
}

/*
 * configures all necessary modules using respective config functions
 */
void config() {
    //timer config
    timer_setup(0,1,CONTROL_LOOP_PERIOD); //control loop timer
    timer_setup(1,0,PROGRAM_LENGTH); //program length timer 
    
    //semaphore that blocks end program task 
    killSemaphore = xSemaphoreCreateBinary();

    gpio_queue = xQueueCreate(10, sizeof(uint32_t));

    memset(f_buf,0,strlen(f_buf));
    memset(err_buf,0,strlen(err_buf));

    if(SENSOR_ENABLE == 1) {
        //adc config
        // adc1_config_width(ADC_WIDTH_BIT_12);
        // adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);
        
        //GPIO config
        // gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);
        
        //i2c and IMU config
        i2c_master_config();
    }
    
}

/*
* creates tasks
*/
void app_main() { 
    config();   
    TaskHandle_t ctrlHandle = NULL;
    TaskHandle_t endHandle = NULL;
    xTaskCreate(control_thread_function, "control_thread_function", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
    xTaskCreate(end_program, "end_program", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle);
}