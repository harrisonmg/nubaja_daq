#ifndef NUBAJA_LSM6DSM_H_
#define NUBAJA_LSM6DSM_H_

#include "nubaja_i2c.h"

//#define IMU_SLAVE_ADDR  0x69
#define IMU_SLAVE_ADDR  0x6a
#define OUTX_L_G        0x22
#define OUTX_H_G        0x23
#define OUTY_L_G        0x24
#define OUTY_H_G        0x25
#define OUTZ_L_G        0x26
#define OUTZ_H_G        0x27
#define OUTX_L_XL       0x28
#define OUTX_H_XL       0x29
#define OUTY_L_XL       0x2a
#define OUTY_H_XL       0x2b
#define OUTZ_L_XL       0x2c
#define OUTZ_H_XL       0x2d
#define CTRL1_XL        0x10
#define CTRL8_XL        0x17
#define CTRL2_G         0x11
#define IMU_GYRO_FS     2000  // full scale: +/- 2000 degrees / sec
#define IMU_GYRO_SCALE  (IMU_GYRO_FS / 32767)
#define IMU_XL_FS       16    // full scale: +/- 16 g's
#define IMU_XL_SCALE    (IMU_XL_FS / 32767)

typedef struct
{
  int port_num;
  int slave_address;
} LSM6DSM;

// create and configure the LSM6DSM IMU
LSM6DSM init_lsm6dsm(int port_num, int slave_address)
{
  printf("init_lsm6dsm -- configuring an LSM6DSM IMU on port %d\n", port_num);
  LSM6DSM dev;
  dev.port_num = port_num;
  dev.slave_address = slave_address;

  uint8_t ODR_XL = 0x90;
  uint8_t FS_XL = 0x04;
  uint8_t LPF1_BW_SEL = 0b0;
  uint8_t BWO_XL = 0b0;
  uint8_t CTRL1_XL_CONFIG = ( ODR_XL | FS_XL | LPF1_BW_SEL | BWO_XL );

  uint8_t LPF2_XL_EN = 0b0;
  uint8_t HPCF_XL = 0b00;
  uint8_t HP_REF_MODE = 0b0;
  uint8_t INPUT_COMPOSITE = 0b0;
  uint8_t HP_SLOPE_XL_EN = 0b0;
  uint8_t LOW_PASS_ON_6D = 0b0;
  uint8_t CTRL8_XL_CONFIG =
    ( LPF2_XL_EN | HPCF_XL | HP_REF_MODE | INPUT_COMPOSITE | HP_SLOPE_XL_EN | 0 | LOW_PASS_ON_6D );

  uint8_t ODR_G = 0x80;
  uint8_t FS_G = 0x04;
  uint8_t FS_125 = 0b0;
  uint8_t CTRL2_G_CONFIG = ( ODR_G | FS_G | FS_125 | 0 );

  i2c_write_byte(port_num, slave_address, CTRL1_XL, CTRL1_XL_CONFIG);
  i2c_write_byte(port_num, slave_address, CTRL8_XL, CTRL8_XL_CONFIG);
  i2c_write_byte(port_num, slave_address, CTRL2_G, CTRL2_G_CONFIG);

  return dev;
}

void imu_read_gyro_xl(LSM6DSM *dev, int16_t *gyro_x, int16_t *gyro_y, int16_t *gyro_z,
                                    int16_t *xl_x, int16_t *xl_y, int16_t *xl_z)
{
  i2c_read_2_bytes_6_lh(dev->port_num, dev->slave_address, OUTX_L_G,
                       (uint16_t *) gyro_x, (uint16_t *) gyro_y, (uint16_t *) gyro_z,
                       (uint16_t *) xl_x, (uint16_t *) xl_y, (uint16_t *) xl_z);
}

#endif  // NUBAJA_LSM6DSM_H_
