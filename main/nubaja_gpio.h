#ifndef NUBAJA_GIPO_H_
#define NUBAJA_GIPO_H_

#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"

#define MPH_GPIO            26             // wheel rpm hall effect sensor
#define RPM_GPIO            27             // engine rpm measurement circuit
#define LOGGING_GPIO        39             // button to start / stop logging
//#define DISPLAY_GPIO        25             // button to cycle display data

#define GPIO_INPUT_PIN_SEL  ((1ULL<<MPH_GPIO) | (1ULL<<RPM_GPIO) \
                            | (1ULL<<LOGGING_GPIO) //| (1ULL<<DISPLAY_GPIO))

#define FLASHER_GPIO        32             // flashing indicator light (not input)

#define TIRE_DIAMETER       22             // inches
#define INCHES_IN_A_MILE    63360

#define SPEED_TIMER_GROUP   TIMER_GROUP_1  // group of speed timer
#define SPEED_TIMER_IDX     0              // index of speed timer
#define SPEED_TIMER_DIVIDER 100            // speed timer prescale divider

#define MAX_RPM             4500           // cut off wacky high errors
#define MAX_MPH             100            // cut off wacky high errors

xQueueHandle rpm_queue;                    // queue for engine rpm values
xQueueHandle mph_queue;                    // queue for wheel speed values

double last_rpm_time = 0;
double last_mph_time = 0;

// button event group bits
#define ENABLE_LOGGING_BIT  (1 << 0)
#define DATA_TO_LOG_BIT     (1 << 1)
#define CYCLE_DISPLAY_BIT   (1 << 2)
EventGroupHandle_t button_eg;  // button press event group (logging, display)

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
    xQueueOverwriteFromISR(mph_queue, &mph, NULL);
  last_mph_time = time;
}

static void rpm_isr_handler(void *arg)
{
  double time;
  timer_get_counter_time_sec(SPEED_TIMER_GROUP, SPEED_TIMER_IDX, &time);
  uint16_t rpm = 60.0 / (time - last_rpm_time);
  if (rpm <= MAX_RPM)
    xQueueOverwriteFromISR(rpm_queue, &rpm, NULL);
  last_rpm_time = time;
}

static void logging_isr_handler(void *arg)
{
  if (xEventGroupGetBitsFromISR(button_eg) & ENABLE_LOGGING_BIT)
  {
    xEventGroupClearBitsFromISR(button_eg, ENABLE_LOGGING_BIT);
  }
  else
  {
    xEventGroupSetBitsFromISR(button_eg,
        ENABLE_LOGGING_BIT | DATA_TO_LOG_BIT, NULL);
  }
}

static void display_isr_handler(void *arg)
{
  xEventGroupSetBitsFromISR(button_eg, CYCLE_DISPLAY_BIT, NULL);
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
  rpm_queue = xQueueCreate(1, sizeof(uint16_t));
  mph_queue = xQueueCreate(1, sizeof(uint16_t));

  // setup button event group
  button_eg = xEventGroupCreate();
  xEventGroupClearBits(button_eg,
      ENABLE_LOGGING_BIT | DATA_TO_LOG_BIT | CYCLE_DISPLAY_BIT);

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

  // wheel hall-effect sensor
  gpio_isr_handler_add(RPM_GPIO, mph_isr_handler, NULL);
  // engine comparator circuit
  gpio_isr_handler_add(MPH_GPIO, rpm_isr_handler, NULL);
  // logging toggle button
  gpio_isr_handler_add(LOGGING_GPIO, logging_isr_handler, NULL);

  // display data cycle button
  //gpio_isr_handler_add(DISPLAY_GPIO, display_isr_handler, NULL);

  // flasher
  gpio_set_direction(FLASHER_GPIO, GPIO_MODE_OUTPUT);
  flasher_off();
}

#endif // NUBAJA_GPIO_H_
