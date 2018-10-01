# NU Baja DAQ System

This program and set of libraries is intended for use as a data aquisition system on the Northeastern Baja race car, using an ESP32.

This project uses the ESP-IDF API in order to implement GPIO and timer interrupts, I2C, and SDMMC.

It is designed such that the drivers that provide these functionalities are somewhat modular and can be used in other projects to tailor them to a particular set of needs.

## Functionality Overview

* Collect data from two GPIO rising edge interupts (RPM and MPH) and an LSM6DSM IMU, and (eventually) a thermistor.
* Display RPM, MPH, or temperature on a 7-segment display using an AS1115 display driver, and cycle displayed data using a GPIO interrupt.
* Enable and disable the recording and writing of all data to a Micro SD card using a GPIO interrupt, with a GPIO output pin to signify data collection.

## Development Setup

Below are instructions to setup development of this project.

* Follow [these instructions](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/) for setting up an ESP32 development environment.
* In the `esp-idf` directory, clone this repository.
```console
ok@computer:~/esp/esp-idf$ git clone https://github.com/harrisonmg/nubaja_daq.git
```
* In the cloned repository, use `make flash monitor` to build the project, flash it to a connected ESP32, and begin the serial monitor.
