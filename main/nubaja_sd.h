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

#define LOGGING_QUEUE_SIZE  1000   // data logging queue size

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
  // num data points * num fields * num and comma/return length
  int buff_size = LOGGING_QUEUE_SIZE * 9 * 7;
  char buff[buff_size];
  while (xQueueReceive(lq, &dp, 0) != pdFALSE)
  {
    snprintf(buff, buff_size,
             "%" PRIu16 ",%" PRIu16 ",%" PRIu16 ","
             "%" PRId16 ",%" PRId16 ",%" PRId16 ","
             "%" PRId16 ",%" PRId16 ",%" PRId16 "\n",
             dp.rpm,    dp.mph,     dp.temp,
             dp.gyro_x, dp.gyro_y,  dp.gyro_z,
             dp.xl_x,   dp.xl_y,    dp.xl_z);
  }
  printf("%s", buff);

  //FILE *fp;
  //fp = fopen("/sdcard/data.txt", "a");
  //if (fp == NULL)
  //{
    //printf("write_logging_queue_to_sd -- failed to create file\n");
    //vTaskDelete(NULL);
  //}
  //fprintf(fp, "%s", buff);
  //fclose(fp);

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
}

#endif // NUBAJA_SD_H_
