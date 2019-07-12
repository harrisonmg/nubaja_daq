#ifndef NUBAJA_SD_H_
#define NUBAJA_SD_H_

#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"

#define SD_MISO 19
#define SD_MOSI 18
#define SD_CLK  14
#define SD_CS   15

#define LOGGING_QUEUE_SIZE  300  // data logging queue size

#define WRITING_DATA_BIT (1 << 0)
EventGroupHandle_t writing_eg;  // event group to signify writing data

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
  if (xEventGroupGetBits(writing_eg) & WRITING_DATA_BIT)
  {
    printf("write_logging_queue_to_sd -- task overlap, skipping queue\n");
    vTaskDelete(NULL);
  }
  else
  {
    xEventGroupSetBits(writing_eg, WRITING_DATA_BIT);
  }

  xQueueHandle lq = (xQueueHandle) arg;
  data_point dp;
  // num fields * num chars for each value + comma / return
  // int16_t can be -35,xxx, so max 6 chars per val
  int line_size = 9 * 7;
  // num lines * line size + char for null term
  int buff_size = LOGGING_QUEUE_SIZE * line_size + 1;
  char* buff = (char*) malloc(buff_size * sizeof(char));

  int i = 0;
  xQueueReceive(lq, &dp, 0);
  while (xQueueReceive(lq, &dp, 0) != pdFALSE)
  {
    snprintf(buff + (i * line_size), buff_size - (i * line_size),
             "%6" PRIu16 ",%6" PRIu16 ",%6" PRIu16 ","
             "%6" PRId16 ",%6" PRId16 ",%6" PRId16 ","
             "%6" PRId16 ",%6" PRId16 ",%6" PRId16 "\n",
             dp.rpm,    dp.mph,     dp.temp,
             dp.gyro_x, dp.gyro_y,  dp.gyro_z,
             dp.xl_x,   dp.xl_y,    dp.xl_z);
    ++i;
  }

  FILE *fp;
  fp = fopen("/sdcard/esp_data.csv", "a");
  if (fp == NULL)
  {
    printf("write_logging_queue_to_sd -- failed to create file\n");
    vTaskDelete(NULL);
  }
  fprintf(fp, "%s", buff);
  fclose(fp);

  printf("write_logging_queue_to_sd -- writing done\n");
  free(buff);
  xEventGroupClearBits(writing_eg, WRITING_DATA_BIT);

  // per FreeRTOS, tasks MUST be deleted before breaking out of its implementing funciton
  vTaskDelete(NULL);
}

void init_sd()
{
  printf("init_sd -- configuring SD storage\n");

  sdmmc_host_t host = SDSPI_HOST_DEFAULT();

  sdspi_slot_config_t slot_config = SDSPI_SLOT_CONFIG_DEFAULT();
  slot_config.gpio_miso = SD_MISO;
  slot_config.gpio_mosi = SD_MOSI;
  slot_config.gpio_sck  = SD_CLK;
  slot_config.gpio_cs   = SD_CS;

  esp_vfs_fat_sdmmc_mount_config_t mount_config =
  {
    .format_if_mount_failed = false,
    .max_files = 5
  };

  sdmmc_card_t* card;
  if (esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card)
      != ESP_OK)
  {
    printf("init_sd -- failed to mount SD card\n");
  }

  // setup button event group
  writing_eg = xEventGroupCreate();
  xEventGroupClearBits(writing_eg, WRITING_DATA_BIT);
}

#endif // NUBAJA_SD_H_
