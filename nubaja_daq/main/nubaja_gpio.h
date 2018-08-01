#ifndef NUBAJA_GIPO_H_
#define NUBAJA_GIPO_H_

#include "freertos/queue.h"
#include "driver/gpio.h"

#define MPH_GPIO                            26  // wheel rpm hall effect sensor
#define RPM_GPIO                            27  // engine rpm measurement circuit
#define LOGGING_GPIO                        32  // button to start / stop logging
#define GPIO_INPUT_PIN_SEL                  ( (1ULL<<MPH_GPIO) | (1ULL<<RPM_GPIO) | (1ULL<<LOGGING_GPIO) )
#define FLASHER_GPIO                        13  // flashing light TODO: switch back to flash pin
#define TIRE_DIAMETER                       22  // inches
#define INCHES_IN_A_MILE                    63360

int ENABLE_LOGGING = 0;
int RPM_FLAG = 1;
int MPH_FLAG = 1;

float RPMFromTicks(int ticks)
{
  if (ticks <= 0)
    return 0;
  return 60.0 / (ticks * portTICK_PERIOD_MS / 1000.0);
}

float MPHFromTicks(int ticks)
{
  return RPMFromTicks(ticks) * 60 * TIRE_DIAMETER / INCHES_IN_A_MILE;
}

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
    MPH_FLAG = 1;
}

static void rpm_isr_handler(void *arg)
{
    RPM_FLAG = 1;
}

static void logging_isr_handler(void *arg)
{
    ENABLE_LOGGING = !ENABLE_LOGGING;
}

// configure gpio pins for input and ISRs, and the flasher pin for output
void configure_gpio(xQueueHandle rpm_queue, xQueueHandle mph_queue)
{
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
    gpio_isr_handler_add(RPM_GPIO, mph_isr_handler, (void *) rpm_queue);
    gpio_isr_handler_add(MPH_GPIO, rpm_isr_handler, (void *) mph_queue);
    gpio_isr_handler_add(LOGGING_GPIO, logging_isr_handler, NULL);

    // flasher
    gpio_set_direction(FLASHER_GPIO, GPIO_MODE_OUTPUT);
    flasher_off();
}

#endif // NUBAJA_GPIO_H_
