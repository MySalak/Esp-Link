#include <Arduino.h>
#include "serial_cmd.h"

// ============================================================
// ESP32 SWD Bit-Bang Programmer for STM32U585
// ============================================================
//
// This firmware turns an ESP32 into an SWD programmer that can
// flash firmware onto an STM32U585CIU6 target via bit-banged
// Serial Wire Debug (SWD) protocol.
//
// Hardware Connections:
//   ESP32 GPIO 22 --> STM32 SWCLK
//   ESP32 GPIO 21 --> STM32 SWDIO  (+ 10kΩ pull-up to 3.3V)
//   ESP32 GPIO 19 --> STM32 NRST
//   ESP32 GND     --> STM32 GND
//
// Usage:
//   1. Flash this firmware to ESP32
//   2. Open serial monitor at 921600 baud
//   3. Type 'help' for available commands
//   4. Type 'connect' to initialize SWD and halt the STM32
//   5. Type 'program 0x08000000 <size>' then send binary data
//   6. Type 'reset' to release the STM32
//

void setup() {
    // Initialize the serial command interface
    serial_cmd_init();
}

void loop() {
    // Process incoming serial commands
    serial_cmd_process();
}