#include <stdio.h>
#include "esp_types.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"

#include "nubaja_gpio.h"
#include "nubaja_i2c.h"
#include "nubaja_as1115.h"
#include "nubaja_lsm6dsm.h"

#define TIMER_DIVIDER   16                                  // hardware timer clock divider
#define TIMER_SCALE     (TIMER_BASE_CLK / TIMER_DIVIDER)    // convert counter val to sec
#define DAQ_TIMER_IDX   0                                   // index of daq timer
#define DAQ_TIMER_HZ    1000                                // frequency of the daq timer in Hz

xQueueHandle timer_queue;   // queue to time the daq task
xQueueHandle rpm_queue;     // queue for engine rpm values
xQueueHandle mph_queue;     // queue for wheel speed values

// interrupt for the daq timer
void IRAM_ATTR daq_timer_isr(void *para)
{
    // retrieve the interrupt status and the counter value from the timer
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    TIMERG0.hw_timer[DAQ_TIMER_IDX].update = 1;

    // clear the interrupt
    if (intr_status & BIT(DAQ_TIMER_IDX))
    {
        TIMERG0.int_clr_timers.t0 = 1;
    }

    // enable the alarm again, so it is triggered the next time
    TIMERG0.hw_timer[DAQ_TIMER_IDX].config.alarm_en = TIMER_ALARM_EN;

    // send the counter value to the queue to trigger the daq task
    xQueueSendFromISR(timer_queue, &intr_status, NULL);
}

static void daq_timer_init()
{
    // select and initialize basic parameters of the timer
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = 1;
    timer_init(TIMER_GROUP_0, DAQ_TIMER_IDX, &config);

    // timer's counter will initially start from value below
    timer_set_counter_value(TIMER_GROUP_0, DAQ_TIMER_IDX, 0x00000000ULL);

    // configure the alarm value and the interrupt on alarm
    timer_set_alarm_value(TIMER_GROUP_0, DAQ_TIMER_IDX, ((double) 1 / DAQ_TIMER_HZ) * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, DAQ_TIMER_IDX);
    timer_isr_register(TIMER_GROUP_0, DAQ_TIMER_IDX, daq_timer_isr,
        NULL, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, DAQ_TIMER_IDX);
}

// task to run the main daq system based on a timer
static void daq_task(void *arg)
{
    // initial config
    // rpm, mph, logging toggle, and flasher are gpio
    configure_gpio(rpm_queue, mph_queue);

    // keep track of ticks for rpm and mph
    int last_rpm_ticks, last_mph_ticks;
    last_rpm_ticks = last_mph_ticks = xTaskGetTickCount();

    // initial values of displayable values
    float rpm, mph, temp;
    rpm = mph = temp = 0;

    // init display
    i2c_master_config(PORT_1, FAST_MODE_PLUS, I2C_MASTER_1_SDA_IO, I2C_MASTER_1_SCL_IO);
    AS1115 display = init_as1115(PORT_1, AS1115_SLAVE_ADDR);

    // init imu
    i2c_master_config(PORT_0, FAST_MODE, I2C_MASTER_0_SDA_IO,I2C_MASTER_0_SCL_IO);
    LSM6DSM imu = init_lsm6dsm(PORT_0, IMU_SLAVE_ADDR);

    // TODO: be rid of debugging
    int last_ticks = 0;

    // wait for timer trigger via a queue addition
    uint32_t intr_status;
    while (1)
    {
        // TODO: be rid of debugging
        int ticks = xTaskGetTickCount();
        printf("%d\n", ticks - last_ticks);
        last_ticks = ticks;

        // wait for 1 kHz timer
        xQueueReceive(timer_queue, &intr_status, portMAX_DELAY);

        // flasher if loggin
        if (ENABLE_LOGGING)
            flasher_on();
        else
            flasher_off();

        // rpm
        if (RPM_FLAG)
        {
            int ticks = xTaskGetTickCount();
            RPM_FLAG = 0;
            rpm = RPMFromTicks(ticks - last_rpm_ticks);
            printf("rpm: %f\n", rpm);
            last_rpm_ticks = ticks;
        }

        // mph
        if (MPH_FLAG)
        {
            int ticks = xTaskGetTickCount();
            MPH_FLAG = 0;
            mph = MPHFromTicks(ticks - last_mph_ticks);
            printf("mph: %f\n", mph);
            last_mph_ticks = ticks;
        }

        // imu
        int16_t gyro_x, gyro_y, gyro_z, xl_x, xl_y, xl_z;
        imu_read_gyro_xl(&imu, &gyro_x, &gyro_y, &gyro_z, &xl_x, &xl_y, &xl_z);
        /*printf("%d, \t%d, \t%d, \t%d, \t%d, \t%d\n", gyro_x, gyro_y, gyro_z, xl_x, xl_y, xl_z);*/

        // display
        // TODO: display value cycle button
        int disp_val = (int) rpm;

        int ones = disp_val % 10;
        int tens = (disp_val /= 10) % 10;
        int hundreds = (disp_val /= 10) % 10;
        int thousands = (disp_val /= 10) % 10;
        /*display_4_digits(&display, ones, tens, hundreds, thousands);*/
    }
    // per FreeRTOS, tasks MUST be deleted before breaking out of its implementing funciton
    vTaskDelete(NULL);
}

// initialize the daq timer and start the daq task
void app_main()
{
    // init queues
    timer_queue = xQueueCreate(1, sizeof(uint32_t));
    rpm_queue = xQueueCreate(5, sizeof(float));
    mph_queue = xQueueCreate(5, sizeof(float));

    // start daq timer and daq task
    daq_timer_init();
    xTaskCreatePinnedToCore(daq_task, "daq_task", 2048,
                            NULL, (configMAX_PRIORITIES-1), NULL, 0);
}
