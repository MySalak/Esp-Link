#ifndef SWD_DP_H
#define SWD_DP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// ARM ADIv5 Debug Port (DP) Register Addresses
// ============================================================
#define DP_IDCODE       0x00  // Read: ID Code
#define DP_ABORT        0x00  // Write: Abort register
#define DP_CTRL_STAT    0x04  // R/W: Control/Status
#define DP_SELECT       0x08  // Write: AP Select
#define DP_RDBUFF       0x0C  // Read: Read Buffer

// CTRL/STAT bit definitions
#define DP_CSYSPWRUPREQ   (1UL << 30)
#define DP_CDBGPWRUPREQ   (1UL << 28)
#define DP_CSYSPWRUPACK   (1UL << 31)
#define DP_CDBGPWRUPACK   (1UL << 29)

// ABORT bit definitions
#define DP_ABORT_DAPABORT  (1UL << 0)
#define DP_ABORT_STKCMPCLR (1UL << 1)
#define DP_ABORT_STKERRCLR (1UL << 2)
#define DP_ABORT_WDERRCLR  (1UL << 3)
#define DP_ABORT_ORUNERRCLR (1UL << 4)

// ============================================================
// AHB-AP Register Offsets (within AP bank)
// ============================================================
#define AP_CSW          0x00  // Control/Status Word
#define AP_TAR          0x04  // Transfer Address Register
#define AP_DRW          0x0C  // Data Read/Write

// CSW values
// 32-bit word access, no auto-increment, DbgSwEnable, privileged
#define AP_CSW_32BIT        0x23000002
// 32-bit word access, auto-increment single, DbgSwEnable, privileged
#define AP_CSW_32BIT_INCR   0x23000012

// ============================================================
// Cortex-M Debug Registers (memory-mapped)
// ============================================================
#define DHCSR_ADDR      0xE000EDF0  // Debug Halting Control/Status
#define DCRSR_ADDR      0xE000EDF4  // Debug Core Register Selector
#define DCRDR_ADDR      0xE000EDF8  // Debug Core Register Data
#define DEMCR_ADDR      0xE000EDFC  // Debug Exception & Monitor Control
#define AIRCR_ADDR      0xE000ED0C  // Application Interrupt & Reset Control

// DHCSR bits
#define DHCSR_DBGKEY    0xA05F0000
#define DHCSR_C_DEBUGEN (1UL << 0)
#define DHCSR_C_HALT    (1UL << 1)
#define DHCSR_C_STEP    (1UL << 2)
#define DHCSR_S_REGRDY  (1UL << 16)
#define DHCSR_S_HALT    (1UL << 17)
#define DHCSR_S_LOCKUP  (1UL << 19)
#define DHCSR_S_RESET_ST (1UL << 25)

// DEMCR bits
#define DEMCR_VC_CORERESET (1UL << 0)
#define DEMCR_TRCENA       (1UL << 24)

// AIRCR bits
#define AIRCR_VECTKEY      0x05FA0000
#define AIRCR_SYSRESETREQ  (1UL << 2)

// ============================================================
// Function Declarations
// ============================================================

/// Full SWD connection: init, JTAG-to-SWD switch, read IDCODE, power up debug
/// @param idcode  Output: device IDCODE (can be NULL)
/// @return true on success
bool swd_connect(uint32_t *idcode);

/// Read a Debug Port register
bool swd_dp_read(uint8_t addr, uint32_t *data);

/// Write a Debug Port register
bool swd_dp_write(uint8_t addr, uint32_t data);

/// Read an Access Port register (handles SELECT + pipeline delay)
bool swd_ap_read(uint8_t addr, uint32_t *data);

/// Write an Access Port register (handles SELECT)
bool swd_ap_write(uint8_t addr, uint32_t data);

/// Read a 32-bit word from target memory via AHB-AP
bool swd_mem_read32(uint32_t addr, uint32_t *data);

/// Write a 32-bit word to target memory via AHB-AP
bool swd_mem_write32(uint32_t addr, uint32_t data);

/// Read a block of memory from target (len must be multiple of 4)
bool swd_mem_read_block(uint32_t addr, uint8_t *data, uint32_t len);

/// Write a block of memory to target (len must be multiple of 4)
bool swd_mem_write_block(uint32_t addr, const uint8_t *data, uint32_t len);

/// Halt the target CPU
bool swd_halt_cpu(void);

/// Resume (unhalt) the target CPU
bool swd_unhalt_cpu(void);

/// Reset target and halt (using DEMCR VC_CORERESET + AIRCR reset)
bool swd_reset_and_halt(void);

/// Reset target and let it run
bool swd_reset_and_run(void);

/// Clear all sticky error flags in DP ABORT register
void swd_clear_errors(void);

#endif // SWD_DP_H
