# MySalak STM32Duino Programmer
**STLong32 · Marlong Black Edition**

A blisteringly fast, ultra-optimized ESP32 SWD Bit-Bang Programmer for STM32 microcontrollers (specifically tuned for STM32U5 series, but extensible).



---

## ⚡ Features
- **Ultra-Fast Uploads:** Heavily optimized C++ SWD bit-banging running at ~2-3 MHz.
- **Hardware Auto-Increment:** Uses AP Block Reads/Writes to halve SWD overhead.
- **High Baud Rate:** Communicates over PC Serial at `460800` baud.
- **Interactive TUI:** A sleek Python Terminal User Interface for flashing and checking connections.
- **Flawless Verification:** Chunked flow-control prevents USB/UART drops for 100% reliable verification.

---

## 🔌 Hardware Connections & Wiring

Since both your **ESP32** and **STM32** operate at **3.3V**, they share the same logic levels. **No level shifters are needed.**

### Pinout Configuration
| ESP32 Pin | STM32 Pin | Connection Note |
| :--- | :--- | :--- |
| **GND** | **GND** | Ground (must be common) |
| **GPIO 22** | **SWCLK** | Clock line |
| **GPIO 21** | **SWDIO** | Bidirectional Data line (10kΩ pull-up to 3.3V recommended) |
| **GPIO 19** | **NRST** | Target Reset line (active low) |
| **GPIO 16 (RX2)** | **TX** | Target UART TX for serial bridge monitor |

---

## 🚀 Usage Instructions

### Step 1: Upload the Programmer Firmware to ESP32
1. Open this project in **VS Code with PlatformIO**.
2. Connect your ESP32 to your PC.
3. Build and upload the code to the ESP32:
   ```bash
   pio run -t upload
   ```

### Step 2: Use the Interactive Python TUI
We have provided an automated, interactive Python application (`programmer/main.py`) that handles connecting, uploading, verifying, and serial monitoring.

1. Install the required python library:
   ```bash
   pip install pyserial colorama
   ```
2. Run the interactive console, pointing to your ESP32's serial port (e.g., `COM6`):
   ```bash
   python programmer/main.py COM6
   ```

### Step 3: Interactive Menu
The application will launch the **Marlong Black Edition** menu:
- `1` - **Connect**: Checks the connection to the STM32 and reads the IDCODE.
- `2` - **Upload**: Uploads the `firmware.bin` from the `./bin` directory.
- `3` - **Upload & Verify**: Uploads the firmware and then streams a flawless high-speed verification pass.
- `4` - **Serial Monitor**: Bridges the STM32's UART TX over the ESP32 to your console.
- `5` - **Erase Flash**: Mass erases the STM32 flash.
- `0` - **Exit**.

---

## 🛠️ Troubleshooting

* **File not found (COM port issue)**:
  Make sure PlatformIO isn't holding onto the COM port in the background. Unplug and replug the ESP32.
* **Verification Mismatches**:
  If you get random mismatches, your jumper wires might be too long. You can slow down the SWD clock by increasing `SWD_DELAY_CYCLES` in `include/pin_config.h` from `8` to `20`.
* **Flash write fails**:
  Ensure the STM32 is not read-out protected. The script automatically unlocks the flash, but deep security settings may require a mass erase.

---

## 🧠 Technical Architecture (C++)

The firmware running on the ESP32 is a highly optimized, custom bare-metal SWD (Serial Wire Debug) driver. 

### 1. SWD Protocol & Bit-Banging (`swd_bitbang.cpp`)
Standard SWD runs via dedicated hardware (like an ST-Link). We emulate this on the ESP32 using GPIO bit-banging. To achieve speeds comparable to dedicated programmers:
- **Zero-Overhead Loops:** Instead of using slow `volatile for` loops for the clock delay, we use compile-time unrolled templates (`template<int N> swd_delay_nops()`). This eliminates branching overhead and creates a perfectly stable ~3 MHz SWD clock out of raw `NOP` assembly instructions.
- **Direct Register Access:** We use ESP32's `w1ts` and `w1tc` GPIO registers for instantaneous pin toggling.

### 2. Pipelined Debug Port (DP) and Access Port (AP) Operations (`swd_dp.cpp`)
The ARM debug interface uses an internal pipeline.
- **Hardware Auto-Increment:** Writing thousands of bytes to flash normally requires sending an address, then data, then an address, then data. We utilize the AP `CSW_ADDRINC_SINGLE` feature. This allows us to send the address *once* (`AP_TAR`), and then rapidly stream data to the `AP_DRW` register. The hardware automatically increments the memory address internally.
- **1KB Boundary Handling:** The ARM specification dictates that auto-increment stops and wraps at 1024-byte boundaries. Our `swd_mem_read_block` and `swd_mem_write_block` logic automatically detects boundary crossings, flushes the read/write pipeline (`DP_RDBUFF`), updates the `AP_TAR` address, and seamlessly primes the pipeline to continue streaming without dropping a single byte.

### 3. Flash Controller Integration (`stm32u5_flash.cpp`)
For the STM32U5 series, writing to flash memory requires specific unlocking sequences:
- We write the `FLASH_KEYR` sequence to unlock the controller.
- **Quad-Word Programming:** The STM32U5 flash requires data to be written in 128-bit (16-byte) quad-words. We use the block-write auto-increment feature to instantly write 4 consecutive 32-bit words over SWD, perfectly triggering the flash controller's internal write-commit sequence.

### 4. High-Speed Serial Flow Control (`serial_cmd.cpp`)
To keep the ESP32 fed with data at `460,800` baud without overflowing the UART buffer:
- The ESP32's hardware RX buffer is expanded to 8KB (`Serial.setRxBufferSize(8192)`).
- We use a strict chunk-based flow-control protocol (`PROGRESS:` ACKs) so the Python TUI streams exactly 1024 bytes at a time. This guarantees 100% data integrity during both flash writes and verifications without requiring hardware RTS/CTS flow control wires.
