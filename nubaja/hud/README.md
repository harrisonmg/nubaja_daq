# PROJECT: NU Baja DAQ System

This program and set of libraries is intended for use as a data aquisition system on the Northeastern Baja race car.
This project uses the ESP-IDF API in order to implement GPIO and timer interrupts, I2C, SPI, and WiFi. 
It is designed such that the drivers that provide these functionalities are modular and can be used in various projects to tailor them to a particular set of needs. 

## Functionality Overview

* Two timers are configured
* The first timer determines the polling rate for the GPIO interrupt OR the frequency of the control loop
     * There are two functions that can be called by the control loop. they are control_inertia() and control_dyno(timer_event_t evt) respectively. These are used according to which data we want to collect. See functions themselves for more detail. 
* The second timer is used to determine vehicle speed and also to determine when to end the program. 
* WiFi can be used to control the start of data recording. Typically, I will create a mobile hotspot with the appropriate SSID and password for the ESP to connect to. I then send a UPD packet via netcat (using a terminal emulator on my phone) that contains the number of seconds I wish the program to run for. At this point, the UDP server is stopped and data recording begins. 
* A "flasher" is used to display to the driver visually that data is recording. This is a small orange lamp that when provided 12V, flashes on and off at a consistent rate. This 12V supply is switched via a relay which is controlled by an N-CH MOSFET which controls the current through the relay coil. 
* Several runmodes are available for easy enabling and disabling of particular features, such as data logging, error logging, or WiFi. The runmodes are designed such that they can easily and quickly be expanded without interfering with previously defined runmodes. 
