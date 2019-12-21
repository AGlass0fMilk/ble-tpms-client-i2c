# BLE TPMS Client

BLE Client for reading TPMS data broadcast by aftermarket "BLE TPMS" sensors commonly available on Amazon/Aliexpress (example shown below).

![tpms](https://github.com/AGlass0fMilk/ble-tpms-client-i2c/blob/master/docs/ble-tpms.png)

This application is built on Mbed-OS and should work for any target that supports BLE. The intended targets are either the Nordic nRF52832 or Nordic nRF52840 Bluetooth SoCs.

This application programs the microcontroller to act as a BLE TPMS client with an API exposed to a host processor over I2C. You are free to modify it if you don't want to use another processor.

My application requires CAN so I am using another processor (STM32F091) that has CAN to interface with the rest of my custom BCU system.



