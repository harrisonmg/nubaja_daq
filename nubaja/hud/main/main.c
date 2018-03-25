//main header
#include "hud_helper.h"

/* drivers
 * if cloning, file path below must be modified according to driver location. 
 */
#include </home/sparky/esp/esp-idf/nubaja/drivers/nubaja_wifi.h>
#include </home/sparky/esp/esp-idf/nubaja/drivers/AS1115_driver.h>
#include </home/sparky/esp/esp-idf/nubaja/drivers/nubaja_logging.h>
#include </home/sparky/esp/esp-idf/nubaja/drivers/ITG_3200_driver.h>

//vars
int COMMS_ENABLE = 0; //initialise with UDP listening 
int SENSOR_ENABLE = 1; //1 = enabled
int LOGGING_ENABLE = 0; //1 = enabled  

SemaphoreHandle_t killSemaphore = NULL;
SemaphoreHandle_t commsSemaphore = NULL;
xQueueHandle timer_queue = NULL;
xQueueHandle gpio_queue = NULL;
static const char *MAIN_TAG = "MAIN";

char f_buf[SIZE]; //DATA BUFFER
char err_buf[SIZE]; //ERROR BUFFER
int buffer_idx = 0;
int err_buffer_idx = 0;

uint64_t old_time = 0;
uint64_t old_time_RPM = 0;
int program_len = 30;
char *DHCP_IP;

/*
 * configures all necessary modules using respective config functions
 */
void config() {
    //timer config
    timer_setup(0,1,CONTROL_LOOP_PERIOD); //control loop timer

    if (COMMS_ENABLE == 1) {

        timer_setup(1,0,program_len); //program length timer 

    } else {

        timer_setup(1,0,PROGRAM_LENGTH); //program length timer 

    }

    //semaphore that blocks end program task 
    killSemaphore = xSemaphoreCreateBinary();

    gpio_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));

    memset(f_buf,0,strlen(f_buf));
    memset(err_buf,0,strlen(err_buf));

    //i2c module config
    i2c_master_config();

    //start confirmation flasher
    gpio_set_direction(FLASHER_GPIO, GPIO_MODE_OUTPUT);
    gpio_set_level(FLASHER_GPIO,1); //activate relay G6L-1F DC3

    if (SENSOR_ENABLE == 1) {
        
        //adc config
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);
        
        //GPIO config
        config_gpio();
        
        //display driver config
        AS1115_config();

        //gyro 
        // itg_3200_config();
    }

    if (LOGGING_ENABLE == 1) {
        
        sd_config();
    
    }
}

/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function polls the GPIO interrupt. if the interrupt is set
 * then the time since the last interrupt is calculated, and this is 
 * subsequently used to calculate vehicle speed 
 * Additionally, a thermistor is measured and its temperature display and recorded 
 */
void control(timer_event_t evt) {
    uint32_t gpio_num;

    if (SENSOR_ENABLE == 1) {

        if ((xQueueReceive(gpio_queue, &gpio_num, 0)) == pdTRUE) { //0 or portMAX_DELAY here?
            
            if (gpio_num == HALL_EFF_GPIO) { //hall effect

                uint64_t curr_time = evt.timer_counts;
                float period = (float) (curr_time - old_time) / TIMER_SCALE;
                float v_car = MPH_SCALE / period;
                old_time = curr_time; 
                display_speed(v_car);
                printf("speed: %f intr: %08x\n",v_car,gpio_num);

            } 

            if (gpio_num == ENGINE_RPM_GPIO) { //engine RPM measuring circuit
                
                uint64_t curr_time_RPM = evt.timer_counts;
                float period = (float) (curr_time_RPM - old_time_RPM) / TIMER_SCALE;
                float RPM = RPM_SCALE / period;
                old_time_RPM = curr_time_RPM; 
                display_RPM(RPM);
                printf("RPM: %f intr: %08x\n",RPM,gpio_num);                 
                // add_32b_to_buffer(f_buf,RPM);
                
            }  

            // ERROR_HANDLE_ME(i2c_read_3_reg(GYRO_SLAVE_ADDR, XH));
                        
            // uint16_t adc_raw = adc1_get_raw(ADC1_CHANNEL_6);  //read ADC (thermistor)
            // add_12b_to_buffer(f_buf,adc_raw); 
            // float adc_v = (float) adc_raw * ADC_SCALE; //convert ADC counts to temperature//this will change when a thermistor is actually spec'd
            // float temp = (adc_v - THERM_B) / THERM_M;
            
            // uint8_t temp_l = (uint32_t) temp % 10; 
            // uint8_t temp_h = ( (uint32_t) temp / 10) % 10; 
            // AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_2,temp_l);
            // AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_3,temp_h);    
        }
    }
}

/*
 * Resets interrupt and calls control function to interface sensors
 */
void control_thread() 
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
void timeout_thread(void* task) {   
    while(1) {
        if (xSemaphoreTake(killSemaphore, portMAX_DELAY) == pdTRUE) //end program after dumping to file
        {
            
            ESP_LOGI(MAIN_TAG, "program timeout expired");
            vTaskPrioritySet((TaskHandle_t*) task,(configMAX_PRIORITIES-2));
            for (int n=0;n<10;n++) {

                vTaskSuspend((TaskHandle_t*) task);
                vTaskDelay(1);

            }

            gpio_kill(1,FLASHER_GPIO);
            ESP_LOGI(MAIN_TAG, "goodbye!");
            vTaskSuspend(NULL);

        }
    }
}

/*
* creates tasks
*/
void app_main() { 
    
    printf("Date :%s\n", __DATE__ );
    printf("Time :%s\n", __TIME__ );

    if (COMMS_ENABLE == 1) {

        commsSemaphore = xSemaphoreCreateBinary();
        wifi_config();   

    } else if (COMMS_ENABLE == 0) {

        commsSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(commsSemaphore);

    }
       
    if (xSemaphoreTake(commsSemaphore, portMAX_DELAY) == pdTRUE) {

        config();
        TaskHandle_t ctrlHandle = NULL;
        TaskHandle_t endHandle = NULL;
        ESP_LOGI(MAIN_TAG, "Creating tasks");
        xTaskCreate(control_thread, "control", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
        xTaskCreate(timeout_thread, "timeout", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle);

    } 

}
