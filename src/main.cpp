#include <Arduino.h>
#include "serial_cmd.h"

// ============================================================
// MySalak STM32Duino Programmer
// STLong32 · Marlong Black Edition (ESP32 SWD Bit-Bang)
//
// @author Farras000
// ============================================================
//
// Hardware Connections:

//   ESP32 GPIO 22 --> STM32 SWCLK
//   ESP32 GPIO 21 --> STM32 SWDIO  (pull-up optional for 3.3V)
//   ESP32 GPIO 19 --> STM32 NRST
//   ESP32 GND     --> STM32 GND
//   ESP32 GPIO 16 <-- STM32 TX     (UART bridge, RX2)
//
// Serial2 (RX2 = GPIO16) reads STM32 UART TX output at 115200
// and forwards every byte to the USB Serial port so the STM32
// debug output is visible in the serial monitor alongside the
// SWD programmer output.
//

// RX2 pin that connects to the STM32 TX line
#define STM32_TX_RX_PIN  16

void setup() {
    // Initialize SWD programmer command interface (uses Serial / USB)
    serial_cmd_init();

    // Initialize Serial2 in RX-only mode to read STM32 UART output.
    // TX2 pin is set to -1 so it is not configured (we only need RX).
    Serial2.begin(115200, SERIAL_8N1, STM32_TX_RX_PIN, -1);
}

void loop() {
    // Process incoming SWD programmer commands from the PC
    serial_cmd_process();

    // Bridge: forward any bytes from STM32 UART → USB Serial
    while (Serial2.available()) {
        Serial.write(Serial2.read());
    }
}