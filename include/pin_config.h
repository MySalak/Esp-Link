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

#define SWCLK_PIN   22
#define SWDIO_PIN   21
#define NRST_PIN    19

// Serial baud rate for PC communication
#define SERIAL_BAUD 921600

// SWD clock delay (NOPs per half-cycle at 240MHz ESP32)
// 32 cycles (~3MHz) is the perfect balance of maximum speed and signal integrity.
#define SWD_DELAY_CYCLES  32

// Maximum firmware chunk size for serial transfer (bytes)
// Must match Python CHUNK_SIZE. Keep at 4096 for reliable flow control.
#define FW_CHUNK_SIZE     4096

// Debug logging enable (comment out to disable)
#define SWD_DEBUG_LOG

#endif // PIN_CONFIG_H
