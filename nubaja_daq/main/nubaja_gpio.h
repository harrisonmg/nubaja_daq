#ifndef NUBAJA_GIPO_H_
#define NUBAJA_GIPO_H_

#include "freertos/queue.h"
#include "driver/gpio.h"

#define MPH_GPIO            26             // wheel rpm hall effect sensor
#define RPM_GPIO            27             // engine rpm measurement circuit
#define LOGGING_GPIO        32             // button to start / stop logging
#define GPIO_INPUT_PIN_SEL  ( (1ULL<<MPH_GPIO) | (1ULL<<RPM_GPIO) | (1ULL<<LOGGING_GPIO) )
#define FLASHER_GPIO        13             // flashing light TODO: switch back to flash pin
#define TIRE_DIAMETER       22             // inches
#define INCHES_IN_A_MILE    63360

#define SPEED_TIMER_GROUP   TIMER_GROUP_0  // group of speed timer
#define SPEED_TIMER_IDX     1              // index of speed timer
#define SPEED_TIMER_DIVIDER 100            // speed timer prescale divider

#define MAX_RPM             4500           // cut off wacky high errors
#define MAX_MPH             100            // cut off wacky high errors

xQueueHandle rpm_queue;                    // queue for engine rpm values
xQueueHandle mph_queue;                    // queue for wheel speed values

double last_rpm_time = 0;
double last_mph_time = 0;

int ENABLE_LOGGING = 0;

void flasher_on()
{
    gpio_set_level(FLASHER_GPIO, 1);
}

void flasher_off()
{
    gpio_set_level(FLASHER_GPIO, 0);
}

static void mph_isr_handler(void *arg)
{
  double time;
  timer_get_counter_time_sec(SPEED_TIMER_GROUP, SPEED_TIMER_IDX, &time);
  uint16_t mph = 60.0 / (time - last_mph_time) * 60 * TIRE_DIAMETER / INCHES_IN_A_MILE;
  if (mph <= MAX_MPH)
    xQueueSendFromISR(mph_queue, &mph, NULL);
  last_mph_time = time;
}

static void rpm_isr_handler(void *arg)
{
  double time;
  timer_get_counter_time_sec(SPEED_TIMER_GROUP, SPEED_TIMER_IDX, &time);
  uint16_t rpm = 60.0 / (time - last_rpm_time);
  if (rpm <= MAX_RPM)
    xQueueSendFromISR(rpm_queue, &rpm, NULL);
  last_rpm_time = time;
}

static void logging_isr_handler(void *arg)
{
    ENABLE_LOGGING = !ENABLE_LOGGING;
}

static void speed_timer_init()
{
    // select and initialize basic parameters of the timer
    timer_config_t config;
    config.divider = SPEED_TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_DIS;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = TIMER_AUTORELOAD_DIS;
    timer_init(SPEED_TIMER_GROUP, SPEED_TIMER_IDX, &config);

    // timer's counter will initially start from value below
    timer_set_counter_value(SPEED_TIMER_GROUP, SPEED_TIMER_IDX, 0x00000000ULL);

    timer_start(SPEED_TIMER_GROUP, SPEED_TIMER_IDX);
}

// configure gpio pins for input and ISRs, and the flasher pin for output
void configure_gpio()
{
    // setup timer and queues for speeds
    speed_timer_init();
    rpm_queue = xQueueCreate(10, sizeof(uint16_t));
    mph_queue = xQueueCreate(10, sizeof(uint16_t));

    // config rising-edge interrupt GPIO pins (rpm, mph, and logging)
    gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_POSEDGE;  // interrupt of rising edge
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;  // bit mask of the pins
    io_conf.mode = GPIO_MODE_INPUT;  // set as input mode
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io_conf);

    // ISRs
    gpio_install_isr_service(0); // install gpio isr service
    gpio_isr_handler_add(RPM_GPIO, mph_isr_handler, NULL);
    gpio_isr_handler_add(MPH_GPIO, rpm_isr_handler, NULL);
    gpio_isr_handler_add(LOGGING_GPIO, logging_isr_handler, NULL);

    // flasher
    gpio_set_direction(FLASHER_GPIO, GPIO_MODE_OUTPUT);
    flasher_off();
}

#endif // NUBAJA_GPIO_H_
