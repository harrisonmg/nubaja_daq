// TODO: rid
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

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

#define DAQ_TIMER_GROUP TIMER_GROUP_0  // group of daq timer
#define DAQ_TIMER_IDX   0              // index of daq timer
#define DAQ_TIMER_HZ    1000           // frequency of the daq timer in Hz

#define DISPLAY_REFRESH_RATE 4         // refresh rate of the 7-seg in Hz

xQueueHandle timer_queue;              // queue to time the daq task

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
    xQueueOverwriteFromISR(timer_queue, &intr_status, NULL);
}

static void daq_timer_init()
{
    // how quickly timer ticks, 80 MHz / divider
    int divider = 100;

    // select and initialize basic parameters of the timer
    timer_config_t config;
    config.divider = divider;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = TIMER_AUTORELOAD_EN;
    timer_init(DAQ_TIMER_GROUP, DAQ_TIMER_IDX, &config);

    // timer's counter will initially start from value below
    timer_set_counter_value(DAQ_TIMER_GROUP, DAQ_TIMER_IDX, 0x00000000ULL);

    // configure the alarm value and the interrupt on alarm
    timer_set_alarm_value(DAQ_TIMER_GROUP, DAQ_TIMER_IDX, TIMER_BASE_CLK / divider / DAQ_TIMER_HZ);
    timer_enable_intr(DAQ_TIMER_GROUP, DAQ_TIMER_IDX);
    timer_isr_register(DAQ_TIMER_GROUP, DAQ_TIMER_IDX, daq_timer_isr, NULL, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(DAQ_TIMER_GROUP, DAQ_TIMER_IDX);
}

// task to run the main daq system based on a timer
static void daq_task(void *arg)
{
    // initial config

    // rpm, mph, logging toggle, and flasher are gpio
    configure_gpio();

    // init display
    i2c_master_config(PORT_1, FAST_MODE_PLUS, I2C_MASTER_1_SDA_IO, I2C_MASTER_1_SCL_IO);
    AS1115 display = init_as1115(PORT_1, AS1115_SLAVE_ADDR);
    int display_throttle_counter = 0;

    // init imu
    i2c_master_config(PORT_0, FAST_MODE, I2C_MASTER_0_SDA_IO,I2C_MASTER_0_SCL_IO);
    LSM6DSM imu = init_lsm6dsm(PORT_0, IMU_SLAVE_ADDR);

    // TODO: spi & sd card (dual data buffer)

    // TODO: be rid of debugging
    int last_ticks = 0;

    uint32_t intr_status;
    while (1)
    {
        // wait for 1 kHz timer alarm
        xQueueReceive(timer_queue, &intr_status, portMAX_DELAY);

        // TODO: be rid of debugging
        int ticks = xTaskGetTickCount();
        /*printf("%d\n", ticks - last_ticks);*/
        last_ticks = ticks;

        // flasher if logging
        // TODO: enable logging button
        flasher_on();
        if (ENABLE_LOGGING)
            flasher_on();
        else
            flasher_off();

        // TODO: check full data buffer

        // TODO: data storage
        // imu
        int16_t gyro_x, gyro_y, gyro_z, xl_x, xl_y, xl_z;
        imu_read_gyro_xl(&imu, &gyro_x, &gyro_y, &gyro_z, &xl_x, &xl_y, &xl_z);
        /*printf("%d, \t%d, \t%d, \t%d, \t%d, \t%d\n", gyro_x, gyro_y, gyro_z, xl_x, xl_y, xl_z);*/

        // display
        // TODO: display value cycle button
        ++display_throttle_counter;
        if (display_throttle_counter >= DAQ_TIMER_HZ / DISPLAY_REFRESH_RATE)
        {
            display_throttle_counter = 0;
            uint16_t disp_val;
            if(xQueuePeek(rpm_queue, &disp_val, 0) == pdPASS)
            {
                int ones = disp_val % 10;
                int tens = (disp_val /= 10) % 10;
                int hundreds = (disp_val /= 10) % 10;
                int thousands = (disp_val /= 10) % 10;
                display_4_digits(&display, thousands, hundreds, tens, ones);
            }
        }
    }
    // per FreeRTOS, tasks MUST be deleted before breaking out of its implementing funciton
    vTaskDelete(NULL);
}

// initialize the daq timer and start the daq task
void app_main()
{
    timer_queue = xQueueCreate(1, sizeof(uint32_t));

    // start daq timer and daq task
    daq_timer_init();
    xTaskCreatePinnedToCore(daq_task, "daq_task", 2048,
                            NULL, (configMAX_PRIORITIES-1), NULL, 0);
}
