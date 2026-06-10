#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// ============================================================
// ESP32 GPIO Pin Assignments for SWD Programming
// ============================================================
//
// ESP32 GPIO 22 ----> STM32 SWCLK
// ESP32 GPIO 21 ----> STM32 SWDIO  (pull-up optional, see README)
// ESP32 GPIO 19 ----> STM32 NRST   (active low)
// ESP32 GND     ----> STM32 GND
//
// NOTE: If the STM32 runs at 1.8V, level shifters are needed!

#define SWCLK_PIN 19
#define SWDIO_PIN 18
#define NRST_PIN 5

// Serial baud rate for PC communication
#define SERIAL_BAUD 2000000

// SWD clock delay (NOPs per half-cycle at 240MHz ESP32)
// Currently set to 32 cycles (~3MHz) for perfect stability WITHOUT a physical pull-up resistor.
#define SWD_DELAY_CYCLES 0

// Maximum firmware chunk size for serial transfer (bytes)
// Must match Python CHUNK_SIZE. Keep at 4096 for reliable flow control.
#define FW_CHUNK_SIZE 65536

// Debug logging enable (comment out to disable)
#define SWD_DEBUG_LOG

#endif // PIN_CONFIG_H
