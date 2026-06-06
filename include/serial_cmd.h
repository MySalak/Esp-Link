#ifndef SERIAL_CMD_H
#define SERIAL_CMD_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// Serial Command Interface
// ============================================================
//
// Text Commands (via serial monitor):
//   help                 - Show available commands
//   connect              - Initialize SWD, read IDCODE, halt CPU
//   idcode               - Read and display IDCODE
//   halt                 - Halt CPU
//   resume               - Resume CPU
//   reset                - Reset target and run
//   read <addr> [count]  - Read memory (hex), count = # of 32-bit words
//   write <addr> <data>  - Write 32-bit word to address (hex)
//   erase <page> [bank]  - Erase flash page (bank 0 or 1)
//   eraseall             - Mass erase all flash
//   program <addr> <size>- Enter binary upload mode
//   checksum <addr> <size>- Calculate CRC32 of flash and return hash
//   disconnect           - Release SWD
//   status               - Show programmer status
//
// Binary Upload Protocol:
//   After "program <addr> <size>", ESP32 responds "READY\r\n"
//   Then expects exactly <size> bytes of raw binary data
//   Responds with "OK\r\n" or "ERROR: <msg>\r\n"

// Maximum command line length
#define CMD_MAX_LEN     128

// Programming state machine
typedef enum {
    STATE_IDLE,           // Waiting for commands
    STATE_CONNECTED,      // SWD connected, target halted
    STATE_PROGRAMMING,    // Receiving firmware data
    STATE_ERROR           // Error state
} ProgrammerState;

// ============================================================
// Function Declarations
// ============================================================

/// Initialize the serial command handler
void serial_cmd_init(void);

/// Process incoming serial data (call from loop())
void serial_cmd_process(void);

/// Get current programmer state
ProgrammerState serial_cmd_get_state(void);

#endif // SERIAL_CMD_H
