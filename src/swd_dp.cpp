#include "swd_dp.h"
#include "swd_bitbang.h"
#include "pin_config.h"
#include <Arduino.h>

// ============================================================
// Internal State
// ============================================================

// Track the last SELECT value to avoid redundant writes
static uint32_t last_select = 0xFFFFFFFF;

// ============================================================
// Debug Logging
// ============================================================

#ifdef SWD_DEBUG_LOG
  #define DBG(fmt, ...) Serial.printf("[SWD] " fmt "\r\n", ##__VA_ARGS__)
#else
  #define DBG(fmt, ...) ((void)0)
#endif

// ============================================================
// Internal Helpers
// ============================================================

/// Execute an SWD transfer with retry on WAIT responses
static uint8_t swd_transfer_retry(uint8_t request, uint32_t *data) {
    uint8_t ack;
    for (int retry = 0; retry < SWD_RETRY_COUNT; retry++) {
        ack = swd_transfer(request, data);
        if (ack != SWD_ACK_WAIT) {
            return ack;
        }
        delayMicroseconds(10);
    }
    DBG("SWD transfer timeout (WAIT)");
    return SWD_ACK_WAIT;
}

/// Write the DP SELECT register (only if value changed)
static bool swd_select(uint32_t select_val) {
    if (select_val == last_select) {
        return true;
    }

    uint8_t req = swd_build_request(SWD_DP, SWD_WRITE, DP_SELECT);
    uint8_t ack = swd_transfer_retry(req, &select_val);
    if (ack != SWD_ACK_OK) {
        DBG("SELECT write failed, ACK=%d", ack);
        return false;
    }

    last_select = select_val;
    return true;
}

// ============================================================
// Debug Port (DP) Register Access
// ============================================================

bool swd_dp_read(uint8_t addr, uint32_t *data) {
    uint8_t req = swd_build_request(SWD_DP, SWD_READ, addr);
    uint8_t ack = swd_transfer_retry(req, data);
    if (ack != SWD_ACK_OK) {
        DBG("DP read addr=0x%02X failed, ACK=%d", addr, ack);
        return false;
    }
    return true;
}

bool swd_dp_write(uint8_t addr, uint32_t data) {
    uint8_t req = swd_build_request(SWD_DP, SWD_WRITE, addr);
    uint8_t ack = swd_transfer_retry(req, &data);
    if (ack != SWD_ACK_OK) {
        DBG("DP write addr=0x%02X val=0x%08lX failed, ACK=%d", addr, data, ack);
        return false;
    }
    return true;
}

// ============================================================
// Access Port (AP) Register Access
// AP reads are "posted" — the first read returns stale data.
// We must read RDBUFF to get the actual value.
// ============================================================

bool swd_ap_read(uint8_t addr, uint32_t *data) {
    // Set SELECT register: APSEL=0, APBANKSEL from addr bits [7:4]
    uint32_t select_val = (addr & 0xF0);  // APBANKSEL | APSEL=0
    if (!swd_select(select_val)) {
        return false;
    }

    // Initiate the AP read (returns previous/stale data)
    uint8_t req = swd_build_request(SWD_AP, SWD_READ, addr);
    uint32_t dummy;
    uint8_t ack = swd_transfer_retry(req, &dummy);
    if (ack != SWD_ACK_OK) {
        DBG("AP read addr=0x%02X initiate failed, ACK=%d", addr, ack);
        return false;
    }

    // Read RDBUFF to get the actual data
    return swd_dp_read(DP_RDBUFF, data);
}

bool swd_ap_write(uint8_t addr, uint32_t data) {
    // Set SELECT register: APSEL=0, APBANKSEL from addr bits [7:4]
    uint32_t select_val = (addr & 0xF0);  // APBANKSEL | APSEL=0
    if (!swd_select(select_val)) {
        return false;
    }

    uint8_t req = swd_build_request(SWD_AP, SWD_WRITE, addr);
    uint8_t ack = swd_transfer_retry(req, &data);
    if (ack != SWD_ACK_OK) {
        DBG("AP write addr=0x%02X val=0x%08lX failed, ACK=%d", addr, data, ack);
        return false;
    }
    return true;
}

// ============================================================
// Memory Access via AHB-AP
// ============================================================

/// Configure AHB-AP for 32-bit word access (no auto-increment)
static bool swd_ap_setup_single(void) {
    return swd_ap_write(AP_CSW, AP_CSW_32BIT);
}

/// Configure AHB-AP for 32-bit word access with auto-increment
static bool swd_ap_setup_block(void) {
    return swd_ap_write(AP_CSW, AP_CSW_32BIT_INCR);
}

bool swd_mem_read32(uint32_t addr, uint32_t *data) {
    if (!swd_ap_setup_single()) return false;

    // Write target address to TAR
    if (!swd_ap_write(AP_TAR, addr)) return false;

    // Read DRW (posted read — data comes in next read)
    // We use ap_read which handles the RDBUFF pipeline
    return swd_ap_read(AP_DRW, data);
}

bool swd_mem_write32(uint32_t addr, uint32_t data) {
    if (!swd_ap_setup_single()) return false;

    // Write target address to TAR
    if (!swd_ap_write(AP_TAR, addr)) return false;

    // Write data to DRW (triggers the actual memory write)
    return swd_ap_write(AP_DRW, data);
}

bool swd_mem_read_block(uint32_t addr, uint8_t *data, uint32_t len) {
    if (len == 0 || (len & 3) != 0) return false;

    if (!swd_ap_setup_block()) return false;

    // Write starting address to TAR
    if (!swd_ap_write(AP_TAR, addr)) return false;

    // Make sure SELECT is pointing to DRW's bank
    uint32_t select_val = (AP_DRW & 0xF0);
    if (!swd_select(select_val)) return false;

    uint32_t *words = (uint32_t *)data;
    uint32_t word_count = len / 4;

    // First AP read (posted — returns stale data, primes the pipeline)
    uint8_t req = swd_build_request(SWD_AP, SWD_READ, AP_DRW);
    uint32_t val;
    uint8_t ack = swd_transfer_retry(req, &val);
    if (ack != SWD_ACK_OK) return false;

    // Subsequent reads return data from the PREVIOUS read
    for (uint32_t i = 0; i < word_count - 1; i++) {
        ack = swd_transfer_retry(req, &val);
        if (ack != SWD_ACK_OK) return false;
        words[i] = val;

        // TAR auto-increments within 1KB boundary
        // If we cross a 1KB boundary, we need to update TAR
        if (((addr + (i + 2) * 4) & 0x3FF) == 0) {
            if (!swd_ap_write(AP_TAR, addr + (i + 2) * 4)) return false;
            // Re-select DRW bank and do another posted read
            if (!swd_select(select_val)) return false;
            ack = swd_transfer_retry(req, &val);
            if (ack != SWD_ACK_OK) return false;
        }
    }

    // Last word comes from RDBUFF
    if (!swd_dp_read(DP_RDBUFF, &val)) return false;
    words[word_count - 1] = val;

    return true;
}

bool swd_mem_write_block(uint32_t addr, const uint8_t *data, uint32_t len) {
    if (len == 0 || (len & 3) != 0) return false;

    if (!swd_ap_setup_block()) return false;

    // Write starting address to TAR
    if (!swd_ap_write(AP_TAR, addr)) return false;

    // Make sure SELECT is pointing to DRW's bank
    uint32_t select_val = (AP_DRW & 0xF0);
    if (!swd_select(select_val)) return false;

    const uint32_t *words = (const uint32_t *)data;
    uint32_t word_count = len / 4;

    uint8_t req = swd_build_request(SWD_AP, SWD_WRITE, AP_DRW);

    for (uint32_t i = 0; i < word_count; i++) {
        uint32_t val = words[i];
        uint8_t ack = swd_transfer_retry(req, &val);
        if (ack != SWD_ACK_OK) {
            DBG("Block write failed at word %lu, ACK=%d", i, ack);
            return false;
        }

        // Handle TAR auto-increment 1KB boundary wrap
        if (((addr + (i + 1) * 4) & 0x3FF) == 0 && (i + 1) < word_count) {
            if (!swd_ap_write(AP_TAR, addr + (i + 1) * 4)) return false;
            if (!swd_select(select_val)) return false;
            req = swd_build_request(SWD_AP, SWD_WRITE, AP_DRW);
        }
    }

    return true;
}

// ============================================================
// CPU Control via Cortex-M Debug Registers
// ============================================================

bool swd_halt_cpu(void) {
    // Write DHCSR: DBGKEY | C_HALT | C_DEBUGEN
    uint32_t val = DHCSR_DBGKEY | DHCSR_C_HALT | DHCSR_C_DEBUGEN;
    if (!swd_mem_write32(DHCSR_ADDR, val)) {
        DBG("Failed to write DHCSR for halt");
        return false;
    }

    // Verify halt: read DHCSR and check S_HALT bit
    for (int i = 0; i < 100; i++) {
        uint32_t dhcsr;
        if (swd_mem_read32(DHCSR_ADDR, &dhcsr)) {
            if (dhcsr & DHCSR_S_HALT) {
                DBG("CPU halted (DHCSR=0x%08lX)", dhcsr);
                return true;
            }
        }
        delayMicroseconds(100);
    }

    DBG("CPU halt verification timeout");
    return false;
}

bool swd_unhalt_cpu(void) {
    // Write DHCSR: DBGKEY | C_DEBUGEN (clear C_HALT)
    uint32_t val = DHCSR_DBGKEY | DHCSR_C_DEBUGEN;
    return swd_mem_write32(DHCSR_ADDR, val);
}

bool swd_reset_and_halt(void) {
    // Step 1: Enable halt-on-reset via DEMCR
    uint32_t demcr;
    if (!swd_mem_read32(DEMCR_ADDR, &demcr)) {
        DBG("Failed to read DEMCR");
        return false;
    }
    demcr |= DEMCR_VC_CORERESET;
    if (!swd_mem_write32(DEMCR_ADDR, demcr)) {
        DBG("Failed to write DEMCR");
        return false;
    }

    // Step 2: Ensure debug is enabled and request halt
    if (!swd_mem_write32(DHCSR_ADDR, DHCSR_DBGKEY | DHCSR_C_DEBUGEN | DHCSR_C_HALT)) {
        DBG("Failed to write DHCSR");
        return false;
    }

    // Step 3: Request system reset via AIRCR
    if (!swd_mem_write32(AIRCR_ADDR, AIRCR_VECTKEY | AIRCR_SYSRESETREQ)) {
        DBG("Failed to write AIRCR");
        // After reset, the debug connection may be temporarily lost
        // This is expected — we need to re-init
    }

    // Step 4: Wait for reset to complete and CPU to halt
    delay(50);  // Give the target time to reset

    // Step 5: Re-initialize SWD connection (reset breaks the DP state)
    swd_line_reset();
    uint32_t idcode;
    if (!swd_dp_read(DP_IDCODE, &idcode)) {
        DBG("Failed to re-read IDCODE after reset");
        return false;
    }

    // Step 6: Re-power debug
    uint32_t ctrl = DP_CSYSPWRUPREQ | DP_CDBGPWRUPREQ;
    if (!swd_dp_write(DP_CTRL_STAT, ctrl)) return false;

    // Wait for power acknowledge
    for (int i = 0; i < 100; i++) {
        uint32_t stat;
        if (swd_dp_read(DP_CTRL_STAT, &stat)) {
            if ((stat & (DP_CSYSPWRUPACK | DP_CDBGPWRUPACK)) ==
                (DP_CSYSPWRUPACK | DP_CDBGPWRUPACK)) {
                break;
            }
        }
        delayMicroseconds(100);
    }

    // Step 7: Verify CPU is halted
    uint32_t dhcsr;
    for (int i = 0; i < 100; i++) {
        if (swd_mem_read32(DHCSR_ADDR, &dhcsr)) {
            if (dhcsr & DHCSR_S_HALT) {
                DBG("CPU halted after reset (DHCSR=0x%08lX)", dhcsr);
                // Clear VC_CORERESET
                swd_mem_read32(DEMCR_ADDR, &demcr);
                demcr &= ~DEMCR_VC_CORERESET;
                swd_mem_write32(DEMCR_ADDR, demcr);
                return true;
            }
        }
        delayMicroseconds(100);
    }

    DBG("CPU did not halt after reset");
    return false;
}

bool swd_reset_and_run(void) {
    // Hardware reset via NRST pin
    swd_target_reset_assert();
    delay(50);
    swd_target_reset_deassert();
    delay(50);
    return true;
}

void swd_clear_errors(void) {
    // Write ABORT register to clear all sticky error flags
    uint32_t abort_val = DP_ABORT_DAPABORT |
                         DP_ABORT_STKCMPCLR |
                         DP_ABORT_STKERRCLR |
                         DP_ABORT_WDERRCLR |
                         DP_ABORT_ORUNERRCLR;
    swd_dp_write(DP_ABORT, abort_val);
    last_select = 0xFFFFFFFF;  // Force re-select
}

// ============================================================
// High-Level Connection
// ============================================================

bool swd_connect(uint32_t *idcode) {
    DBG("Initializing SWD connection...");

    // Reset the last_select tracker
    last_select = 0xFFFFFFFF;

    // Initialize GPIO pins
    swd_init();
    delay(10);

    // JTAG-to-SWD switch sequence
    swd_jtag_to_swd();

    // Read IDCODE (first transaction must be IDCODE read)
    uint32_t id;
    if (!swd_dp_read(DP_IDCODE, &id)) {
        DBG("Failed to read IDCODE");
        return false;
    }
    DBG("IDCODE = 0x%08lX", id);
    if (idcode) *idcode = id;

    // Clear any pending errors
    swd_clear_errors();

    // Power up debug: set CSYSPWRUPREQ and CDBGPWRUPREQ
    uint32_t ctrl = DP_CSYSPWRUPREQ | DP_CDBGPWRUPREQ;
    if (!swd_dp_write(DP_CTRL_STAT, ctrl)) {
        DBG("Failed to write CTRL/STAT for power-up");
        return false;
    }

    // Poll CTRL/STAT until power acknowledged
    for (int i = 0; i < 100; i++) {
        uint32_t stat;
        if (swd_dp_read(DP_CTRL_STAT, &stat)) {
            if ((stat & (DP_CSYSPWRUPACK | DP_CDBGPWRUPACK)) ==
                (DP_CSYSPWRUPACK | DP_CDBGPWRUPACK)) {
                DBG("Debug powered up (CTRL/STAT=0x%08lX)", stat);

                // Clear errors again after power-up
                swd_clear_errors();

                DBG("SWD connection established");
                return true;
            }
        }
        delayMicroseconds(100);
    }

    DBG("Debug power-up timeout");
    return false;
}
