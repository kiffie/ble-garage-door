# Bluetooth Low Energy garage door opener remote control

This is yet another Bluetooth Low Energy (BLE) remote control application that
can be used to control a garage door drive from a smartphone.

A particularity of this project is that BLE advertising PDUs are used,
thereby avoiding connection setup delays. The system thus responds as
quickly to a remote control command as classical hardware garage door
transmitter receiver combination would do. There is one-way communication
from the smartphone to the receiver only, which is secured by HMAC. The idea
was to keep everything simple, both the hardware as well as the software.

## Building the receiver software

1. Make sure that the following is installed
  * Arm GCC toolchain
  * Nordic Semiconductor nRF5 SDK v17.0.0
  * GNU make and Python3

2. `cd nrf52/acn52832_s132`

3. Adapt SDK_ROOT, GNU_INSTALL_ROOT and GNU_VERSION in the Makefile

4. `make`

5. Flash the soft device (s132_nrf52_7.0.1_softdevice.hex), which is part of the SDK

6. Flash the compiled software image (_build/nrf52832_xxaa.hex)

## Building the Android App

1. Make sure that Android Studio and an Android SDK is installed

2. Use Android Studio/Gradle to build the App
