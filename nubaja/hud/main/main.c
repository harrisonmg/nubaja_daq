//main header
#include "hud_helper.h"

/* drivers
 * if cloning, file path below must be modified according to driver location. 
 */
#include "../../drivers/nubaja_wifi.h"
#include "../../drivers/AS1115_driver.h"
#include "../../drivers/nubaja_logging.h"
#include "../../drivers/ITG_3200_driver.h"
#include "../../drivers/LSM6DSM_driver.h"
#include "../../drivers/nubaja_runmodes.h"
#include "../../drivers/nubaja_adc.h"

//run mode - see nubaja_runmodes.h for enumeration
Runmode_t runMode = (Runmode_t) LAB; 
int COMMS_ENABLE;
int SENSOR_ENABLE;
int LOGGING_ENABLE;

//vars
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

    //i2c module configs
    // i2c_master_config(PORT_0,FAST_MODE, I2C_MASTER_0_SDA_IO,I2C_MASTER_0_SCL_IO); //for IMU / GYRO
    i2c_master_config(PORT_1,FAST_MODE, I2C_MASTER_1_SDA_IO,I2C_MASTER_1_SCL_IO); //for AS1115
        
    //start confirmation flasher
    flasher_init(FLASHER_GPIO);
    flasher(H);

    if ( SENSOR_ENABLE ) {
        
        //adc config
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(X_ACCEL, ATTENUATION);
        adc1_config_channel_atten(Y_ACCEL, ATTENUATION);
        adc1_config_channel_atten(Z_ACCEL, ATTENUATION);
        
        //GPIO config
        config_gpio();
        
        //display driver config
        AS1115_config(PORT_1);

        //gyro 
        // itg_3200_config();

        //IMU
        // LSM6DSM_config();

    }

    if ( LOGGING_ENABLE ) {
        
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
void control_dyno(timer_event_t evt) {
    uint32_t gpio_num;

    if ( SENSOR_ENABLE ) {
        
        if ((xQueueReceive(gpio_queue, &gpio_num, 0)) == pdTRUE) { //0 or portMAX_DELAY here?
            
            if (gpio_num == HALL_EFF_GPIO) { //hall effect

                uint64_t curr_time = evt.timer_counts;
                float period = (float) (curr_time - old_time) / TIMER_SCALE;
                float v_car = MPH_SCALE / period;
                old_time = curr_time; 
                display_speed(PORT_1, v_car);
                printf("speed: %f intr: %08x\n",v_car,gpio_num);

            } 

            if (gpio_num == ENGINE_RPM_GPIO) { //engine RPM measuring circuit
                
                uint64_t curr_time_RPM = evt.timer_counts;
                float period = (float) (curr_time_RPM - old_time_RPM) / TIMER_SCALE;
                float RPM = RPM_SCALE / period;
                old_time_RPM = curr_time_RPM; 
                display_RPM(PORT_1, RPM);
                printf("RPM: %f intr: %08x\n",RPM,gpio_num);                 
                // add_32b_to_buffer(f_buf,RPM);
                
            }   
        }
    }
}

/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function reads several sensors and records the data
 */
void control_inertia() {

    if ( SENSOR_ENABLE ) {

        // read_adc1(3,X_ACCEL,Y_ACCEL,Z_ACCEL);
        itg_3200_test(PORT_0, GYRO_SLAVE_ADDR, XH);
        // ERROR_HANDLE_ME(i2c_read_3_reg(PORT_0, GYRO_SLAVE_ADDR, XH,NULL));
        // ERROR_HANDLE_ME(i2c_read_3_reg(PORT_0, IMU_SLAVE_ADDR, OUTX_L_G,NULL));

        // uint16_t adc_raw = adc1_get_raw(TEMP);  //read ADC (thermistor)
        // add_12b_to_buffer(f_buf,adc_raw); 
        // float adc_v = (float) adc_raw * ADC_SCALE; //convert ADC counts to temperature//this will change when a thermistor is actually spec'd
        // float temp = (adc_v - THERM_B) / THERM_M;

        // uint8_t temp_l = (uint32_t) temp % 10; 
        // uint8_t temp_h = ( (uint32_t) temp / 10) % 10; 
        // AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_2,temp_l);
        // AS1115_display_write(AS1115_SLAVE_ADDR,DIGIT_3,temp_h);    
        
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
            control_dyno(evt);
            // control_inertia();
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
            dump_to_file(f_buf,err_buf,1);
            flasher(L);
            // gpio_kill(1,FLASHER_GPIO);
            ESP_LOGI(MAIN_TAG, "goodbye!");
            vTaskSuspend(NULL);

        }
    }
}

/*
* creates tasks
*/
void app_main() { 
    
    //set run mode
    COMMS_ENABLE = (runMode & BIT(2)) >> 2; 
    SENSOR_ENABLE = (runMode & BIT(1)) >> 1; 
    LOGGING_ENABLE = (runMode & BIT(0));  
    ESP_LOGI(MAIN_TAG,"Comms en is: %d",COMMS_ENABLE);
    ESP_LOGI(MAIN_TAG,"Sensor enable is: %d",SENSOR_ENABLE);
    ESP_LOGI(MAIN_TAG,"Logging enable is: %d",LOGGING_ENABLE);

    //INIT UDP SERVER FOR WIFI CONTROL (OR NOT)
    if (COMMS_ENABLE == 1) {

        commsSemaphore = xSemaphoreCreateBinary();
        wifi_config();   

    } else if (COMMS_ENABLE == 0) {

        commsSemaphore = xSemaphoreCreateBinary();
        xSemaphoreGive(commsSemaphore);

    }
       
    //DO MAIN TASKS
    if (xSemaphoreTake(commsSemaphore, portMAX_DELAY) == pdTRUE) {

        config();
        TaskHandle_t ctrlHandle = NULL;
        TaskHandle_t endHandle = NULL;
        ESP_LOGI(MAIN_TAG, "Creating tasks");
        xTaskCreate(control_thread, "control", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
        xTaskCreate(timeout_thread, "timeout", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle);

    } 

}
