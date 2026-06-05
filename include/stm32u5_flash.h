#ifndef STM32U5_FLASH_H
#define STM32U5_FLASH_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
// STM32U585 Flash Memory Layout
// ============================================================
#define FLASH_BASE_ADDR     0x08000000
#define FLASH_PAGE_SIZE     8192        // 8 KB per page
#define FLASH_PAGES_PER_BANK 128
#define FLASH_BANK1_BASE    0x08000000
#define FLASH_BANK2_BASE    0x08100000
#define FLASH_TOTAL_SIZE    (2 * 1024 * 1024)  // 2 MB total

// Quadword programming unit: 128 bits = 16 bytes = 4 x 32-bit words
#define FLASH_QUAD_WORD_SIZE 16

// ============================================================
// Flash Register Addresses (Non-Secure)
// ============================================================
#define FLASH_REG_BASE      0x40022000

#define FLASH_ACR           (FLASH_REG_BASE + 0x00)
#define FLASH_NSKEYR        (FLASH_REG_BASE + 0x08)   // NS Key Register
#define FLASH_OPTKEYR       (FLASH_REG_BASE + 0x10)   // Option Key Register
#define FLASH_NSSR          (FLASH_REG_BASE + 0x20)   // NS Status Register
#define FLASH_NSCR          (FLASH_REG_BASE + 0x28)   // NS Control Register

// Flash unlock keys
#define FLASH_KEY1          0x45670123
#define FLASH_KEY2          0xCDEF89AB

// ============================================================
// FLASH_NSSR (Non-Secure Status Register) Bits
// ============================================================
#define FLASH_NSSR_EOP      (1UL << 0)   // End of operation
#define FLASH_NSSR_OPERR    (1UL << 1)   // Operation error
#define FLASH_NSSR_PROGERR  (1UL << 3)   // Programming error
#define FLASH_NSSR_WRPERR   (1UL << 4)   // Write protection error
#define FLASH_NSSR_PGAERR   (1UL << 5)   // Programming alignment error
#define FLASH_NSSR_SIZERR   (1UL << 6)   // Size error
#define FLASH_NSSR_PGSERR   (1UL << 7)   // Programming sequence error
#define FLASH_NSSR_BSY      (1UL << 16)  // Busy

// Mask for all error bits
#define FLASH_NSSR_ERRORS   (FLASH_NSSR_OPERR | FLASH_NSSR_PROGERR | \
                             FLASH_NSSR_WRPERR | FLASH_NSSR_PGAERR | \
                             FLASH_NSSR_SIZERR | FLASH_NSSR_PGSERR)

// ============================================================
// FLASH_NSCR (Non-Secure Control Register) Bits
// ============================================================
#define FLASH_NSCR_PG       (1UL << 0)   // Programming
#define FLASH_NSCR_PER      (1UL << 1)   // Page erase
#define FLASH_NSCR_MER1     (1UL << 2)   // Bank 1 mass erase
#define FLASH_NSCR_PNB_POS  3            // Page number bit position
#define FLASH_NSCR_PNB_MASK (0xFFUL << FLASH_NSCR_PNB_POS)  // 8-bit page number
#define FLASH_NSCR_BKER     (1UL << 11)  // Bank selection (0=Bank1, 1=Bank2)
#define FLASH_NSCR_MER2     (1UL << 15)  // Bank 2 mass erase
#define FLASH_NSCR_STRT     (1UL << 16)  // Start operation
#define FLASH_NSCR_LOCK     (1UL << 31)  // Lock bit

// ============================================================
// Function Declarations
// ============================================================

/// Unlock the STM32U5 flash for programming
/// @return true on success
bool stm32u5_flash_unlock(void);

/// Lock the flash (re-enable write protection)
/// @return true on success
bool stm32u5_flash_lock(void);

/// Wait for flash operation to complete (BSY flag clear)
/// @param timeout_ms  Maximum wait time in milliseconds
/// @return true if BSY cleared, false on timeout
bool stm32u5_flash_wait_busy(uint32_t timeout_ms);

/// Clear all flash error flags
/// @return true on success
bool stm32u5_flash_clear_errors(void);

/// Erase a single flash page
/// @param bank  Bank number (0 = Bank1, 1 = Bank2)
/// @param page  Page number (0-127)
/// @return true on success
bool stm32u5_flash_erase_page(uint8_t bank, uint8_t page);

/// Mass erase all flash (both banks)
/// @return true on success
bool stm32u5_flash_mass_erase(void);

/// Program a quadword (16 bytes) to flash
/// @param addr  Target flash address (must be 16-byte aligned)
/// @param data  Pointer to 16 bytes of data
/// @return true on success
bool stm32u5_flash_write_quadword(uint32_t addr, const uint8_t *data);

/// Program a block of data to flash
/// @param addr  Target flash address (must be 16-byte aligned)
/// @param data  Pointer to data buffer
/// @param len   Data length in bytes (will be padded to 16-byte boundary)
/// @return true on success
bool stm32u5_flash_write(uint32_t addr, const uint8_t *data, uint32_t len);

/// Verify flash contents against a data buffer
/// @param addr  Flash address to verify
/// @param data  Expected data
/// @param len   Data length in bytes
/// @return true if flash matches data
bool stm32u5_flash_verify(uint32_t addr, const uint8_t *data, uint32_t len);

/// Calculate how many pages need to be erased for a given firmware size
/// @param start_addr  Start address in flash
/// @param size        Firmware size in bytes
/// @param start_page  Output: first page to erase
/// @param page_count  Output: number of pages to erase
/// @param bank        Output: bank number (0 or 1)
void stm32u5_flash_calc_pages(uint32_t start_addr, uint32_t size,
                               uint8_t *start_page, uint8_t *page_count,
                               uint8_t *bank);

#endif // STM32U5_FLASH_H
