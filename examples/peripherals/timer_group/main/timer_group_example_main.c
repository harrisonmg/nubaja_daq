//standard c shite
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "driver/adc.h"
#include "esp_system.h"
#include "esp_adc_cal.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/i2c.h"
#include "soc/timer_group_struct.h"

//TIMER CONFIGS
#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define CONTROL_LOOP_FREQUENCY   (2)   // control loop period for timer group 0 timer 0 in seconds
#define PROGRAM_LENGTH 30 // program length for timer group 0 timer 1 in seconds

//ADC CONFIGS
#define V_REF   1000
#define V_FS 3.6 //change accordingly to ADC_ATTEN_xx_x
#define ADC_SCALE (V_FS/4096)
#define ATTENUATION ADC_ATTEN_11db

//buffer config
#define SIZE 1000
#define HEX 16
char f_buf[SIZE];

//sad card spi config
#define PIN_NUM_MISO 2
#define PIN_NUM_MOSI 15
#define PIN_NUM_CLK  14
#define PIN_NUM_CS   13

//I2C CONFIG
#define I2C_EXAMPLE_MASTER_SCL_IO          22               /*!< gpio number for I2C master clock */
#define I2C_EXAMPLE_MASTER_SDA_IO          23               /*!< gpio number for I2C master data  */
#define I2C_NUM                            I2C_NUM_0        /*!< I2C port number for master dev */
#define I2C_EXAMPLE_MASTER_TX_BUF_DISABLE  0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_RX_BUF_DISABLE  0                /*!< I2C master do not need buffer */
#define I2C_EXAMPLE_MASTER_FREQ_HZ         100000           /*!< I2C master clock frequency */
#define WRITE_BIT                          I2C_MASTER_WRITE /*!< I2C master write */
#define READ_BIT                           I2C_MASTER_READ  /*!< I2C master read */
#define ACK_CHECK_EN                       0x1              /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS                      0x0              /*!< I2C master will not check ack from slave */
#define ACK_VAL                            0x0              /*!< I2C ack value */
#define NACK_VAL                           0x1              /*!< I2C nack value */
#define DATA_LENGTH                        2                //in bytes
#define I2C_TASK_LENGTH                    500              //in ms

//global vars
int level = 0;
int tic = 0;
SemaphoreHandle_t killSemaphore = NULL;
static const char *TAG = "bois";
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

static void i2c_master_config() {
    ESP_LOGI(TAG, "i2c_master_config");
    int i2c_master_port = I2C_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_EXAMPLE_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.scl_io_num = I2C_EXAMPLE_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_DISABLE;//
    conf.master.clk_speed = I2C_EXAMPLE_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    i2c_driver_install(i2c_master_port, conf.mode,
                       I2C_EXAMPLE_MASTER_RX_BUF_DISABLE,
                       I2C_EXAMPLE_MASTER_TX_BUF_DISABLE, 0);
}


// static void i2c_write(uint8_t slave_addr, uint8_t* data_wr, size_t data_len)
// {
//     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//     i2c_master_start(cmd);
//     i2c_master_write_byte(cmd, ( slave_addr << 1 ) | WRITE_BIT, ACK_CHECK_EN);
//     i2c_master_write(cmd, data_wr, data_len, ACK_CHECK_EN);
//     i2c_master_stop(cmd);
//     i2c_master_cmd_begin(I2C_NUM, cmd, 1000 / portTICK_RATE_MS); //esp_err_t ret = 
//     i2c_cmd_link_delete(cmd);
// }

// static void i2c_read(uint8_t slave_addr, uint8_t* data_rd, size_t data_len)
// {
//     // if (data_len == 0) {
//     //     return ESP_OK;
//     // }
//     i2c_cmd_handle_t cmd = i2c_cmd_link_create();
//     i2c_master_start(cmd);
//     i2c_master_write_byte(cmd, ( slave_addr << 1 ) | READ_BIT, ACK_CHECK_EN);
//     if (data_len > 1) {
//         i2c_master_read(cmd, data_rd, data_len - 1, ACK_VAL);
//     }
//     i2c_master_read_byte(cmd, data_rd + data_len - 1, NACK_VAL);
//     i2c_master_stop(cmd);
//     i2c_master_cmd_begin(I2C_NUM, cmd, 1000 / portTICK_RATE_MS); //    esp_err_t ret = 
//     i2c_cmd_link_delete(cmd);
//     // return ret;
// }

//reads who_am_i register of sensor and configures it
static void itg_test() 
{
    ESP_LOGI(TAG, "itg_test");
    int ret;
    // uint8_t* data_wr = (uint8_t*) malloc(DATA_LENGTH);
    // uint8_t* data_rd = (uint8_t*) malloc(DATA_LENGTH);
    uint8_t* who_am_i = (uint8_t*) malloc(1);
    uint8_t gyro_slave_address = 0x69; //general call 
    // for (int i = 0; i < DATA_LENGTH; ++i) {
    //     data_wr[i] = 0xf; //change this accordingly
    // }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);    
    i2c_master_write_byte(cmd, ( gyro_slave_address << 1 ) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write_byte(cmd, 0x0, ACK_VAL); //register 0
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, ( gyro_slave_address << 1 ) | READ_BIT, ACK_CHECK_EN);
    i2c_master_read_byte(cmd, who_am_i, NACK_VAL);
    i2c_master_stop(cmd);
    ret = i2c_master_cmd_begin(I2C_NUM, cmd, I2C_TASK_LENGTH / portTICK_RATE_MS); //how long does this take?
    i2c_cmd_link_delete(cmd);  
    if (ret != ESP_OK) {
        printf("i2c read failed\n");
    } else if (ret == ESP_ERR_INVALID_ARG) {
        printf("parameter error\n");
    }
    else {  
    printf("itg addr daddyyyyyy: %02x\n", *who_am_i);
    }
}

static void sd_config() 
{
    ESP_LOGI(TAG, "sd_config");
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_miso = PIN_NUM_MISO;
    slot_config.gpio_mosi = PIN_NUM_MOSI;
    slot_config.gpio_sck  = PIN_NUM_CLK;
    slot_config.gpio_cs   = PIN_NUM_CS;    
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem; suspending task");
            vTaskSuspend(NULL);
            // ESP_LOGE(TAG, "Failed to mount filesystem. "
            //     "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card");
            vTaskSuspend(NULL);
        }
        return;
    }
}

//configures necessary modules for proper operation
void config() {
    ESP_LOGI(TAG, "config");
    //adc config
    // adc1_config_width(ADC_WIDTH_BIT_12);
    // adc1_config_channel_atten(ADC1_CHANNEL_6, ATTENUATION);

    // //GPIO config
    // gpio_set_direction(GPIO_NUM_4, GPIO_MODE_OUTPUT);

    //timer config
    /* Select and initialize basic parameters of the timer */
    //timer init args: timer index, auto reload, timer length
    timer_setup(0,1,CONTROL_LOOP_FREQUENCY); //control loop timer
    timer_setup(1,0,PROGRAM_LENGTH); //control loop timer 

    killSemaphore = xSemaphoreCreateBinary();

    i2c_master_config();
}


/*
*/
void dump_to_file(char buf[]) {
    ESP_LOGI(TAG, "dump_to_file");
    char nullstr[] = "\0";
    strcpy(buf,nullstr);
    FILE *fp;
    fp = fopen("/sdcard/test.txt", "a");
    if (fp == NULL)
    {
        ESP_LOGE(TAG, "error opening file, suspending task");
        vTaskSuspend(NULL);
    }   
    fputs(buf, fp);
    ESP_LOGI(TAG, "dumped");
    fclose(fp);
    esp_vfs_fat_sdmmc_unmount();
}

void add_int_to_buffer (char buf[],int i_to_add) {
    // ESP_LOGI(TAG, "add_int_to_buffer");
    char str_to_add [sizeof(int)*8+1];
    itoa(i_to_add,str_to_add,HEX);
    printf("%s\n",str_to_add);
    strcpy(buf,str_to_add);
}

//function executed each time ctrl_intr is set 
void control() {
    // ESP_LOGI(TAG, "control");
    // gpio_set_level(GPIO_NUM_4, level);
    // level = !level;
    // int val_0 = adc1_get_raw(ADC1_CHANNEL_6); //* ADC_SCALE
    // add_int_to_buffer(f_buf,val_0);
    itg_test();
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
    ESP_LOGI(TAG, "gpio_kill");
    gpio_set_level(pin, 0);
    gpio_set_direction(pin, GPIO_MODE_INPUT);  
}

//blocks until semaphore is given from program timer ISR
static void end_program(void* task) {    
    if (xSemaphoreTake( killSemaphore, portMAX_DELAY ) == pdTRUE) //end program after dumping to file
    {
        ESP_LOGI(TAG, "end_program");
        vTaskSuspend((TaskHandle_t*) task);
        // vTaskDelay(100); //delay for .1s
        // // dump_to_file(f_buf); 
        // gpio_kill(GPIO_NUM_4); //disable GPIO
        ESP_LOGI(TAG, "suspending task");
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

