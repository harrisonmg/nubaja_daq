#include "hud_helper.h"
///home/sparky/esp/esp-idf/examples/peripherals/hud/main/
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
uint64_t old_time = 0;


/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function contains all functions to read data from any & all sensors
 */
void control(timer_event_t evt) {
    if(SENSOR_ENABLE == 1) {
        uint32_t gpio_num;
        if ((xQueueReceive(gpio_queue, &gpio_num, 0)) == pdTRUE) { //0 or portMAX_DELAY here?
            uint64_t curr_time = evt.timer_counts;
            float period = (float) (curr_time - old_time) / TIMER_SCALE;
            float v_car = 4.10 / period;
            old_time = curr_time; 
            // uint8_t v_car_l = (uint32_t) v_car % 10; 
            // uint8_t v_car_h = ( (uint32_t) v_car/10) % 10; 
            //AS1115_display_write(0x0,v_car_l);
            //AS1115_display_write(0x1,v_car_h);
            printf("period   : %.8f s\n", period);
            printf("v_car: %u mph\n", (uint32_t) v_car);
            // printf("v_car_l: %u\n", v_car_l);
            // printf("v_car_h: %u\n", v_car_h);
                        
            uint16_t adc_raw = adc1_get_raw(ADC1_CHANNEL_6);  //read ADC (thermistor)
            float adc_v = (float) adc_raw * ADC_SCALE; //convert ADC counts to temperature//this will change when a thermistor is actually spec'd
            float temp = (adc_v - THERM_B) / THERM_M;
            //AS1115_display_write(temp...)      
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
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));

    memset(f_buf,0,strlen(f_buf));
    memset(err_buf,0,strlen(err_buf));

    if(SENSOR_ENABLE == 1) {
        //adc config
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);
        
        //GPIO config
        config_gpio();
        
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