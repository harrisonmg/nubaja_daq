#ifndef NUBAJA_SD_H_
#define NUBAJA_SD_H_

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

typedef struct
{
  uint16_t rpm, mph, temp;
  int16_t gyro_x, gyro_y, gyro_z;
  int16_t xl_x, xl_y, xl_z;
} data_point;

void print_data_point(data_point *dp)
{
  printf("rpm:\t%" PRIu16     "\tmph:\t%" PRIu16    "\ttemp:\t%" PRIu16 "\n"
         "gyro_x:\t%" PRId16  "\tgyro_y:\t%" PRId16 "\tgyro_z:\t%" PRId16 "\n"
         "xl_x:\t%" PRId16    "\txl_y:\t%" PRId16   "\txl_z:\t%" PRId16 "\n",
         dp->rpm,             dp->mph,              dp->temp,
         dp->gyro_x,          dp->gyro_y,           dp->gyro_z,
         dp->xl_x,            dp->xl_y,             dp->xl_z);
}

static void write_logging_queue_to_sd(void *arg)
{
  xQueueHandle lq = (xQueueHandle) arg;
  data_point dp;
  while (xQueueReceive(lq, &dp, 0) != pdFALSE)
  {
    // TODO: write data_points to sd card.
  }
  // per FreeRTOS, tasks MUST be deleted before breaking out of its implementing funciton
  vTaskDelete(NULL);
}

#endif // NUBAJA_SD_H_
