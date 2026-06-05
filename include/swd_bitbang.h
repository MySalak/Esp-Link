#ifndef SWD_BITBANG_H
#define SWD_BITBANG_H

#include <stdint.h>

// ============================================================
// SWD Protocol Constants
// ============================================================

// ACK responses (3-bit, LSB first)
#define SWD_ACK_OK      0x01
#define SWD_ACK_WAIT    0x02
#define SWD_ACK_FAULT   0x04
#define SWD_ACK_NONE    0x07  // No response / protocol error

// APnDP bit values
#define SWD_DP          0     // Debug Port access
#define SWD_AP          1     // Access Port access

// RnW bit values
#define SWD_WRITE       0
#define SWD_READ        1

// Maximum retries on WAIT response
#define SWD_RETRY_COUNT 100

// ============================================================
// Function Declarations
// ============================================================

/// Initialize GPIO pins for SWD communication
void swd_init(void);

/// Perform SWD line reset (50+ clocks with SWDIO high, then 2 idle)
void swd_line_reset(void);

/// Send JTAG-to-SWD switching sequence
void swd_jtag_to_swd(void);

/// Build an SWD request byte from components
/// @param APnDP  SWD_DP or SWD_AP
/// @param RnW    SWD_READ or SWD_WRITE
/// @param addr   Register address (bits 2:3 are used)
/// @return Complete 8-bit request byte
uint8_t swd_build_request(uint8_t APnDP, uint8_t RnW, uint8_t addr);

/// Execute a single SWD transfer (read or write)
/// @param request  8-bit request byte (built by swd_build_request)
/// @param data     Pointer to 32-bit data (read into or written from)
/// @return ACK response (SWD_ACK_OK, SWD_ACK_WAIT, SWD_ACK_FAULT)
uint8_t swd_transfer(uint8_t request, uint32_t *data);

/// Assert NRST (pull low) to hold target in reset
void swd_target_reset_assert(void);

/// Deassert NRST (release high) to let target run
void swd_target_reset_deassert(void);

#endif // SWD_BITBANG_H
