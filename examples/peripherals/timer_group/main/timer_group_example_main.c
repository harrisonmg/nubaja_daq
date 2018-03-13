#include "nubaja_esp32_helper.h" //all other includes located here
#include </home/sparky/esp/esp-idf/examples/peripherals/nubaja/nubaja_wifi.h>

#define SENSOR_ENABLE 1 //0 or 1


//global vars
int level = 0;
SemaphoreHandle_t killSemaphore = NULL;
SemaphoreHandle_t commsSemaphore = NULL;
xQueueHandle ctrl_queue;
const char *MAIN_TAG = "ESP3";
char f_buf[SIZE];
char err_buf[SIZE];
int buffer_idx = 0;
int comms_en = 1; //initialise with wifi running
int program_len = 30;
char *DHCP_IP;


/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function contains all functions to read data from any & all sensors
 */
void control() {
    // ESP_LOGI(MAIN_TAG, "ctrl");
    if(SENSOR_ENABLE == 1) {
        // read_adc(1,ADC1_CHANNEL_6); //first argument is number of arguments
        // ERROR_HANDLE_ME(itg_read(XH));
        // ERROR_HANDLE_ME(itg_read(YH));
        // ERROR_HANDLE_ME(itg_read(ZH));
        ERROR_HANDLE_ME(itg_read_3_reg(XH));
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
        if ((xQueueReceive(ctrl_queue, &evt, 0)) == pdTRUE) 
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
        if (xSemaphoreTake(killSemaphore, portMAX_DELAY) == pdTRUE) //end program after dumping to file
        {
            ESP_LOGI(MAIN_TAG, "end_program");
            vTaskPrioritySet((TaskHandle_t*) task,(configMAX_PRIORITIES-2));
            for (int n=0;n<10;n++) {
                vTaskSuspend((TaskHandle_t*) task);
                vTaskDelay(1);
            }
            ERROR_HANDLE_ME(dump_to_file(f_buf,err_buf,1)); 
            ESP_LOGI(MAIN_TAG, "goodbye!");
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
    timer_setup(1,0,program_len); //program length timer 
    
    //semaphore that blocks end program task 
    killSemaphore = xSemaphoreCreateBinary();


    ctrl_queue = xQueueCreate(10, sizeof(timer_event_t));

    memset(f_buf,0,strlen(f_buf));
    memset(err_buf,0,strlen(err_buf));

    if(SENSOR_ENABLE == 1) {
        //adc config
        // adc1_config_width(ADC_WIDTH_BIT_12);
        // adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);
        
        //SD config 
        ERROR_HANDLE_ME(sd_config()); 

        //GPIO config
        // gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);


        
        //i2c and IMU config
        i2c_master_config();
        itg_3200_config();
    }
    
}

/*
* creates tasks
*/
void app_main() { 

    if (comms_en == 1) {
        commsSemaphore = xSemaphoreCreateBinary();
        wifi_config();     
    }
       
    if (xSemaphoreTake(commsSemaphore, portMAX_DELAY) == pdTRUE) {
        config();   
        TaskHandle_t ctrlHandle = NULL;
        TaskHandle_t endHandle = NULL;
        xTaskCreate(control_thread_function, "control_thread_function", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
        xTaskCreate(end_program, "end_program", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle);
    }
    
}
