# PROJECT: IMU Datalogger

This example uses timers, an SD card, and the I2C module to read a particular set of registers from an I2C IMU device in order to measure vehicle dynamics and record them and store them on the SD card. 

## Functionality Overview

* Two timers are configured. One timer sets the period of the control loop. The other time is used to determine timeout of the program. When the timeout expires, the program stops recording and suspends all tasks. 
* The I2C module is configured as a master device as the IMU is a slave device. Several I2C writes are done upon startup to configure the IMU for proper operation. 
* Each I2C read automatically adds the values to a buffer. When the buffer is full, it is emptied to the file. This serves to reduce the number of SD card accesses, as this time is orders of magnitude longer than the control loop period. 



