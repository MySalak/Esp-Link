#include "stm32u5_flash.h"
#include "swd_dp.h"
#include "pin_config.h"
#include <Arduino.h>
#include <string.h>

// ============================================================
// Debug Logging
// ============================================================

#ifdef SWD_DEBUG_LOG
  #define DBG(fmt, ...) Serial.printf("[FLASH] " fmt "\r\n", ##__VA_ARGS__)
#else
  #define DBG(fmt, ...) ((void)0)
#endif

// ============================================================
// Flash Operations
// ============================================================

bool stm32u5_flash_wait_busy(uint32_t timeout_ms) {
    uint32_t start = millis();

    while ((millis() - start) < timeout_ms) {
        uint32_t nssr;
        if (!swd_mem_read32(FLASH_NSSR, &nssr)) {
            DBG("Failed to read NSSR");
            return false;
        }

        if (!(nssr & FLASH_NSSR_BSY)) {
            // Check for errors
            if (nssr & FLASH_NSSR_ERRORS) {
                DBG("Flash error detected: NSSR=0x%08lX", nssr);
                return false;
            }
            return true;  // Not busy, no errors
        }

        delayMicroseconds(100);
    }

    DBG("Flash busy timeout (%lu ms)", timeout_ms);
    return false;
}

bool stm32u5_flash_clear_errors(void) {
    // Read NSSR and write back any error bits to clear them
    uint32_t nssr;
    if (!swd_mem_read32(FLASH_NSSR, &nssr)) {
        return false;
    }

    if (nssr & FLASH_NSSR_ERRORS) {
        // Writing 1 to error bits clears them
        if (!swd_mem_write32(FLASH_NSSR, nssr & FLASH_NSSR_ERRORS)) {
            return false;
        }
        DBG("Cleared error flags: 0x%08lX", nssr & FLASH_NSSR_ERRORS);
    }

    return true;
}

bool stm32u5_flash_unlock(void) {
    DBG("Unlocking flash...");

    // Read current NSCR to check if already unlocked
    uint32_t nscr;
    if (!swd_mem_read32(FLASH_NSCR, &nscr)) {
        DBG("Failed to read NSCR");
        return false;
    }

    if (!(nscr & FLASH_NSCR_LOCK)) {
        DBG("Flash is already unlocked (NSCR=0x%08lX)", nscr);
        stm32u5_flash_clear_errors();
        return true;
    }

    // Write unlock key sequence to FLASH_NSKEYR
    // KEY1 first, then KEY2
    if (!swd_mem_write32(FLASH_NSKEYR, FLASH_KEY1)) {
        DBG("Failed to write KEY1");
        return false;
    }

    if (!swd_mem_write32(FLASH_NSKEYR, FLASH_KEY2)) {
        DBG("Failed to write KEY2");
        return false;
    }

    // Verify unlock by reading NSCR — LOCK bit should be cleared
    if (!swd_mem_read32(FLASH_NSCR, &nscr)) {
        DBG("Failed to read NSCR after unlock");
        return false;
    }

    DBG("NSCR after unlock = 0x%08lX", nscr);

    if (nscr & FLASH_NSCR_LOCK) {
        DBG("Flash failed to unlock!");
        return false;
    }

    // Clear any pending errors
    stm32u5_flash_clear_errors();

    return true;
}

bool stm32u5_flash_lock(void) {
    DBG("Locking flash...");

    // Read current NSCR
    uint32_t nscr;
    if (!swd_mem_read32(FLASH_NSCR, &nscr)) {
        return false;
    }

    // Set LOCK bit (bit 31)
    nscr |= (1UL << 31);
    return swd_mem_write32(FLASH_NSCR, nscr);
}

bool stm32u5_flash_erase_page(uint8_t bank, uint8_t page) {
    DBG("Erasing bank %d page %d...", bank, page);

    // Wait for flash to be ready
    if (!stm32u5_flash_wait_busy(1000)) {
        DBG("Flash busy before erase");
        return false;
    }

    // Clear error flags
    stm32u5_flash_clear_errors();

    // Build NSCR value:
    // PER = 1 (page erase)
    // PNB = page number
    // BKER = bank
    uint32_t nscr = FLASH_NSCR_PER |
                    ((uint32_t)(page & 0x7F) << FLASH_NSCR_PNB_POS) |
                    (bank ? FLASH_NSCR_BKER : 0);

    if (!swd_mem_write32(FLASH_NSCR, nscr)) {
        DBG("Failed to write NSCR for erase setup");
        return false;
    }

    // Set STRT bit to trigger the erase
    nscr |= FLASH_NSCR_STRT;
    if (!swd_mem_write32(FLASH_NSCR, nscr)) {
        DBG("Failed to set STRT bit");
        return false;
    }

    // Wait for erase to complete (page erase takes ~20ms typical)
    if (!stm32u5_flash_wait_busy(5000)) {
        DBG("Erase timeout for bank %d page %d", bank, page);
        return false;
    }

    // Clear the PER bit
    if (!swd_mem_write32(FLASH_NSCR, 0)) {
        return false;
    }

    DBG("Erase complete: bank %d page %d", bank, page);
    return true;
}

bool stm32u5_flash_mass_erase(void) {
    DBG("Mass erasing all flash...");

    // Wait for flash to be ready
    if (!stm32u5_flash_wait_busy(1000)) {
        return false;
    }

    stm32u5_flash_clear_errors();

    // Set MER1 + MER2 + STRT to erase both banks
    uint32_t nscr = FLASH_NSCR_MER1 | FLASH_NSCR_MER2 | FLASH_NSCR_STRT;
    if (!swd_mem_write32(FLASH_NSCR, nscr)) {
        DBG("Failed to trigger mass erase");
        return false;
    }

    // Mass erase can take up to 25 seconds
    if (!stm32u5_flash_wait_busy(30000)) {
        DBG("Mass erase timeout");
        return false;
    }

    // Clear control bits
    swd_mem_write32(FLASH_NSCR, 0);

    DBG("Mass erase complete");
    return true;
}

bool stm32u5_flash_write_quadword(uint32_t addr, const uint8_t *data) {
    // Address must be 16-byte aligned
    if (addr & 0x0F) {
        DBG("Unaligned quad-word address: 0x%08lX", addr);
        return false;
    }

    // Wait for flash to be ready
    if (!stm32u5_flash_wait_busy(1000)) {
        return false;
    }

    // Set PG (Programming) bit in NSCR
    if (!swd_mem_write32(FLASH_NSCR, FLASH_NSCR_PG)) {
        DBG("Failed to set PG bit");
        return false;
    }

    // Write 4 consecutive 32-bit words (128 bits total) using block write
    // The STM32U5 flash controller starts programming automatically
    // after all 4 words are written.
    if (!swd_mem_write_block(addr, data, 16)) {
        DBG("Failed to write quad-word at 0x%08lX", addr);
        // Clear PG bit
        swd_mem_write32(FLASH_NSCR, 0);
        return false;
    }

    // Wait for programming to complete
    if (!stm32u5_flash_wait_busy(1000)) {
        DBG("Programming timeout at 0x%08lX", addr);
        swd_mem_write32(FLASH_NSCR, 0);
        return false;
    }

    // Clear PG bit
    if (!swd_mem_write32(FLASH_NSCR, 0)) {
        return false;
    }

    return true;
}

bool stm32u5_flash_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    if (len == 0) return true;

    DBG("Programming %lu bytes at 0x%08lX...", len, addr);

    // Align address down to 16-byte boundary
    if (addr & 0x0F) {
        DBG("Warning: address 0x%08lX not 16-byte aligned", addr);
        return false;
    }

    uint32_t bytes_written = 0;

    while (bytes_written < len) {
        // Prepare 16-byte quadword buffer
        uint8_t qword[FLASH_QUAD_WORD_SIZE];
        uint32_t remaining = len - bytes_written;
        uint32_t chunk = (remaining >= FLASH_QUAD_WORD_SIZE) ?
                         FLASH_QUAD_WORD_SIZE : remaining;

        // Copy data, pad with 0xFF if less than 16 bytes
        memcpy(qword, data + bytes_written, chunk);
        if (chunk < FLASH_QUAD_WORD_SIZE) {
            memset(qword + chunk, 0xFF, FLASH_QUAD_WORD_SIZE - chunk);
        }

        // Program the quadword
        if (!stm32u5_flash_write_quadword(addr + bytes_written, qword)) {
            DBG("Failed to program at 0x%08lX", addr + bytes_written);
            return false;
        }

        bytes_written += FLASH_QUAD_WORD_SIZE;

        // Progress indicator every 1KB
        if ((bytes_written % 1024) == 0) {
            DBG("Programmed %lu / %lu bytes", bytes_written, len);
        }
    }

    DBG("Programming complete: %lu bytes written", len);
    return true;
}

bool stm32u5_flash_verify(uint32_t addr, const uint8_t *data, uint32_t len) {
    DBG("Verifying %lu bytes at 0x%08lX...", len, addr);

    // Read back in 4-byte chunks and compare
    uint32_t verified = 0;
    const uint32_t *expected_words = (const uint32_t *)data;

    while (verified < len) {
        uint32_t read_val;
        if (!swd_mem_read32(addr + verified, &read_val)) {
            DBG("Verify read failed at 0x%08lX", addr + verified);
            return false;
        }

        uint32_t remaining = len - verified;
        if (remaining >= 4) {
            // Full word comparison
            if (read_val != expected_words[verified / 4]) {
                DBG("Verify mismatch at 0x%08lX: expected 0x%08lX, got 0x%08lX",
                    addr + verified, expected_words[verified / 4], read_val);
                return false;
            }
            verified += 4;
        } else {
            // Partial word at the end
            uint8_t *read_bytes = (uint8_t *)&read_val;
            for (uint32_t i = 0; i < remaining; i++) {
                if (read_bytes[i] != data[verified + i]) {
                    DBG("Verify mismatch at byte 0x%08lX", addr + verified + i);
                    return false;
                }
            }
            verified += remaining;
        }

        // Progress every 4KB
        if ((verified % 4096) == 0 && verified > 0) {
            DBG("Verified %lu / %lu bytes", verified, len);
        }
    }

    DBG("Verification passed: %lu bytes OK", len);
    return true;
}

void stm32u5_flash_calc_pages(uint32_t start_addr, uint32_t size,
                               uint8_t *start_page, uint8_t *page_count,
                               uint8_t *bank) {
    // Determine which bank
    if (start_addr >= FLASH_BANK2_BASE) {
        *bank = 1;
        uint32_t offset = start_addr - FLASH_BANK2_BASE;
        *start_page = offset / FLASH_PAGE_SIZE;
    } else {
        *bank = 0;
        uint32_t offset = start_addr - FLASH_BANK1_BASE;
        *start_page = offset / FLASH_PAGE_SIZE;
    }

    // Calculate number of pages needed
    uint32_t end_addr = start_addr + size - 1;
    uint32_t end_page_offset;

    if (start_addr >= FLASH_BANK2_BASE) {
        end_page_offset = end_addr - FLASH_BANK2_BASE;
    } else {
        end_page_offset = end_addr - FLASH_BANK1_BASE;
    }

    uint8_t end_page = end_page_offset / FLASH_PAGE_SIZE;
    *page_count = end_page - *start_page + 1;

    DBG("Pages to erase: bank=%d, start=%d, count=%d", *bank, *start_page, *page_count);
}
