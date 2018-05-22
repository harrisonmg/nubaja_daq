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

/* TODO: 

Try upping the i2c clock speed for the display driver
Investigate multi-byte writes to further increase speed 
Log uint64_t instead of float conversions 

*/

//run mode - see nubaja_runmodes.h for enumeration
Runmode_t runMode = (Runmode_t) LAB_LOG_ERR; 
int COMMS_ENABLE;
int SENSOR_ENABLE;
int LOGGING_ENABLE;
int ERROR_ENABLE;

//vars
SemaphoreHandle_t killSemaphore = NULL;
SemaphoreHandle_t commsSemaphore = NULL;
xQueueHandle timer_queue = NULL;
xQueueHandle mph_queue = NULL;
xQueueHandle rpm_queue = NULL;
static const char *MAIN_TAG = "MAIN";

char f_buf[SIZE]; //DATA BUFFER
char err_buf[SIZE]; //ERROR BUFFER
int buffer_idx = 0;
int err_buffer_idx = 0;

uint64_t old_time = 0;
uint64_t old_time_RPM = 0;
int program_len = 60;
char *DHCP_IP;

int disp_count = 0;

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

    mph_queue = xQueueCreate(10, sizeof(uint32_t));
    rpm_queue = xQueueCreate(10, sizeof(uint32_t));
    timer_queue = xQueueCreate(10, sizeof(timer_event_t));

    memset(f_buf,0,strlen(f_buf));
    memset(err_buf,0,strlen(err_buf));

    //i2c module configs
    i2c_master_config(PORT_0,FAST_MODE, I2C_MASTER_0_SDA_IO,I2C_MASTER_0_SCL_IO); //for IMU / GYRO
        
    //start confirmation flasher
    flasher_init(FLASHER_GPIO);
    flasher(H);

    if ( SENSOR_ENABLE ) {
        
        //adc config
        adc1_config_width(ADC_WIDTH_BIT_12);
        adc1_config_channel_atten(TEMP, ATTENUATION);
        
        //GPIO config
        config_gpio();
        
        //gyro 
        // itg_3200_config();

        //IMU
        LSM6DSM_config();

    }

    if ( LOGGING_ENABLE || ERROR_ENABLE ) {
        
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
    uint32_t gpio_num_mph;
    uint32_t gpio_num_rpm;

    if ( SENSOR_ENABLE ) {
        
        if ((xQueueReceive(mph_queue, &gpio_num_mph, 0)) == pdTRUE) { 
            
            uint64_t curr_time = evt.timer_counts;
            uint64_t period_speed = curr_time - old_time;

            // double period_speed = (double) (curr_time - old_time) / TIMER_SCALE;
            // double v_car = (double) MPH_SCALE / period_speed;

            old_time = curr_time; 

            // display_speed(PORT_1, v_car);
            // printf("speed: %f intr: %08x\n",v_car,gpio_num);

            // add_32b_to_buffer(f_buf,v_car );
            add_uint64_t_to_buffer(f_buf, period_speed);

        }

        if ((xQueueReceive(rpm_queue, &gpio_num_rpm, 0)) == pdTRUE) {

            uint64_t curr_time_RPM = evt.timer_counts;
            uint64_t period_RPM = curr_time_RPM - old_time_RPM;

            // double period_RPM = (double) (curr_time_RPM - old_time_RPM) / TIMER_SCALE;
            // double RPM = (double) RPM_SCALE / period_RPM;

            old_time_RPM = curr_time_RPM; 
            
            // disp_count++;
            // if (disp_count > 5) {
            //     disp_count = 0;
            //     display_RPM(PORT_1, RPM);
            // }
            
            // printf("RPM: %f intr: %08x\n",RPM,gpio_num);    

            // add_32b_to_err_buffer(err_buf,RPM);
            add_uint64_t_to_err_buffer(err_buf, period_RPM);

        }

        // uint16_t adc_raw = adc1_get_raw(TEMP);  //read ADC (thermistor)
        // add_12b_to_buffer(f_buf,adc_raw); 
        // float adc_v = (float) adc_raw * ADC_SCALE; //convert ADC counts to temperature
        // float temp = (adc_v - THERM_B) / THERM_M;
        // printf("temp: %f\n",temp);                 
        // display_temp(PORT_1,temp);
    }
}

/*
 * This function is executed each time timer 0 ISR sets ctrl_intr high upon timer alarm
 * This function reads several sensors and records the data
 */
void control_inertia() {

    if ( SENSOR_ENABLE ) {
        
        // itg_3200_read(PORT_0, GYRO_SLAVE_ADDR, XH);
        // LSM6DSM_read(PORT_0, IMU_SLAVE_ADDR, OUTX_L_G);
        // LSM6DSM_read(PORT_0, IMU_SLAVE_ADDR, OUTX_L_XL);
        LSM6DSM_read_both(PORT_0, IMU_SLAVE_ADDR, OUTX_L_G);      

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
            err_to_file(err_buf,1); //err_to_file called first here since data_to_file actually unmounts the SD card 
            data_to_file(f_buf,1);
            flasher(L);
            display_hex_word(PORT_1,AS1115_SLAVE_ADDR,0xd,0xe,0xa,0xd);
            display_disable(PORT_1);
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
    ERROR_ENABLE = (runMode & BIT(3)) >> 3; 
    COMMS_ENABLE = (runMode & BIT(2)) >> 2; 
    SENSOR_ENABLE = (runMode & BIT(1)) >> 1; 
    LOGGING_ENABLE = (runMode & BIT(0));  
    ESP_LOGI(MAIN_TAG,"Comms enable is: %d",COMMS_ENABLE);
    ESP_LOGI(MAIN_TAG,"Sensor enable is: %d",SENSOR_ENABLE);
    ESP_LOGI(MAIN_TAG,"Logging enable is: %d",LOGGING_ENABLE);
    ESP_LOGI(MAIN_TAG,"Error enable is: %d",ERROR_ENABLE);

    //init display
    i2c_master_config(PORT_1,FAST_MODE_PLUS, I2C_MASTER_1_SDA_IO,I2C_MASTER_1_SCL_IO); //for AS1115
    AS1115_config(PORT_1);

    //INIT UDP SERVER FOR WIFI CONTROL (OR NOT)
    if (COMMS_ENABLE == 1) {

        commsSemaphore = xSemaphoreCreateBinary();
        display_hex_word(PORT_1,AS1115_SLAVE_ADDR,0xf,0xe,0xe,0xd);   
        wifi_config();        

    } else if (COMMS_ENABLE == 0) {

        commsSemaphore = xSemaphoreCreateBinary(); 
        display_hex_word(PORT_1,AS1115_SLAVE_ADDR,0xd,0xa,0xd,0x5);   
        xSemaphoreGive(commsSemaphore);

    }
       
    //DO MAIN TASKS
    if (xSemaphoreTake(commsSemaphore, portMAX_DELAY) == pdTRUE) {

        display_hex_word(PORT_1,AS1115_SLAVE_ADDR,0xf,0xe,0xd,0xd); 
        config();
        TaskHandle_t ctrlHandle = NULL;
        TaskHandle_t endHandle = NULL;
        ESP_LOGI(MAIN_TAG, "Creating tasks");
        xTaskCreate(control_thread, "control", 2048, NULL, (configMAX_PRIORITIES-1), &ctrlHandle);
        xTaskCreate(timeout_thread, "timeout", 2048, ctrlHandle, (configMAX_PRIORITIES-2),&endHandle);

    } 

}
