#include "serial_cmd.h"
#include "swd_bitbang.h"
#include "swd_dp.h"
#include "stm32u5_flash.h"
#include "pin_config.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// ============================================================
// Internal State
// ============================================================

static ProgrammerState state = STATE_IDLE;
static char cmd_buffer[CMD_MAX_LEN];
static uint16_t cmd_pos = 0;

// Binary upload state
static uint32_t program_addr = 0;
static uint32_t program_size = 0;
static uint32_t program_received = 0;
static uint8_t  fw_chunk[FW_CHUNK_SIZE];
static uint32_t chunk_pos = 0;

// ============================================================
// Helper: Parse hex value from string
// ============================================================

static uint32_t parse_hex(const char *str) {
    uint32_t val = 0;
    // Skip "0x" or "0X" prefix
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str += 2;
    }
    while (*str) {
        char c = *str++;
        val <<= 4;
        if (c >= '0' && c <= '9')      val |= (c - '0');
        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
        else break;
    }
    return val;
}

/// Parse decimal value from string
static uint32_t parse_dec(const char *str) {
    return strtoul(str, NULL, 10);
}

/// Parse a number (auto-detect hex if starts with 0x)
static uint32_t parse_number(const char *str) {
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        return parse_hex(str);
    }
    return parse_dec(str);
}

// ============================================================
// Command Handlers
// ============================================================

static void cmd_help(void) {
    Serial.println(F("=== ESP32 SWD Programmer for STM32U585 ==="));
    Serial.println(F("Commands:"));
    Serial.println(F("  help                  - Show this help"));
    Serial.println(F("  connect               - Init SWD, read IDCODE, halt CPU"));
    Serial.println(F("  idcode                - Read IDCODE"));
    Serial.println(F("  halt                  - Halt CPU"));
    Serial.println(F("  resume                - Resume CPU"));
    Serial.println(F("  reset                 - Reset target and run"));
    Serial.println(F("  read <addr> [count]   - Read memory (hex), count=words"));
    Serial.println(F("  write <addr> <data>   - Write 32-bit word (hex)"));
    Serial.println(F("  erase <page> [bank]   - Erase flash page"));
    Serial.println(F("  eraseall              - Mass erase all flash"));
    Serial.println(F("  program <addr> <size> - Upload firmware (binary)"));
    Serial.println(F("  verify <addr> <size>  - Verify flash (binary)"));
    Serial.println(F("  unlock                - Unlock flash"));
    Serial.println(F("  lock                  - Lock flash"));
    Serial.println(F("  status                - Show programmer status"));
    Serial.println(F("  disconnect            - Release SWD"));
    Serial.println();
    Serial.printf("  GPIO: SWCLK=%d, SWDIO=%d, NRST=%d\r\n", SWCLK_PIN, SWDIO_PIN, NRST_PIN);
}

static void cmd_connect(void) {
    Serial.println(F("Connecting via SWD..."));
    uint32_t idcode;
    if (swd_connect(&idcode)) {
        Serial.printf("OK: IDCODE=0x%08lX\r\n", idcode);
        Serial.println(F("Halting CPU..."));
        if (swd_halt_cpu()) {
            Serial.println(F("OK: CPU halted"));
            state = STATE_CONNECTED;
        } else {
            Serial.println(F("ERROR: Failed to halt CPU"));
            state = STATE_ERROR;
        }
    } else {
        Serial.println(F("ERROR: SWD connection failed"));
        Serial.println(F("Check wiring: SWDIO, SWCLK, GND"));
        state = STATE_ERROR;
    }
}

static void cmd_idcode(void) {
    uint32_t idcode;
    if (swd_dp_read(DP_IDCODE, &idcode)) {
        Serial.printf("OK: IDCODE=0x%08lX\r\n", idcode);

        // Also try to read DBGMCU_IDCODE for STM32-specific ID
        uint32_t dbgmcu;
        if (swd_mem_read32(0xE0044000, &dbgmcu)) {
            Serial.printf("  DBGMCU_IDCODE=0x%08lX (DEV_ID=0x%03lX, REV_ID=0x%04lX)\r\n",
                          dbgmcu, dbgmcu & 0xFFF, (dbgmcu >> 16) & 0xFFFF);
        }
    } else {
        Serial.println(F("ERROR: Failed to read IDCODE"));
    }
}

static void cmd_halt(void) {
    if (swd_halt_cpu()) {
        Serial.println(F("OK: CPU halted"));
        state = STATE_CONNECTED;
    } else {
        Serial.println(F("ERROR: Failed to halt CPU"));
    }
}

static void cmd_resume(void) {
    if (swd_unhalt_cpu()) {
        Serial.println(F("OK: CPU resumed"));
    } else {
        Serial.println(F("ERROR: Failed to resume CPU"));
    }
}

static void cmd_reset(void) {
    Serial.println(F("Resetting target..."));
    swd_reset_and_run();
    Serial.println(F("OK: Target reset"));
    state = STATE_IDLE;
}

static void cmd_read(const char *args) {
    // Parse: <addr> [count]
    char addr_str[20], count_str[20];
    int n = sscanf(args, "%19s %19s", addr_str, count_str);

    if (n < 1) {
        Serial.println(F("Usage: read <addr> [count]"));
        return;
    }

    uint32_t addr = parse_number(addr_str);
    uint32_t count = (n >= 2) ? parse_number(count_str) : 1;

    if (count > 64) count = 64;  // Limit output

    for (uint32_t i = 0; i < count; i++) {
        uint32_t val;
        uint32_t a = addr + i * 4;
        if (swd_mem_read32(a, &val)) {
            Serial.printf("0x%08lX: 0x%08lX\r\n", a, val);
        } else {
            Serial.printf("0x%08lX: ERROR\r\n", a);
            break;
        }
    }
}

static void cmd_write(const char *args) {
    // Parse: <addr> <data>
    char addr_str[20], data_str[20];
    if (sscanf(args, "%19s %19s", addr_str, data_str) != 2) {
        Serial.println(F("Usage: write <addr> <data>"));
        return;
    }

    uint32_t addr = parse_number(addr_str);
    uint32_t data = parse_number(data_str);

    if (swd_mem_write32(addr, data)) {
        Serial.printf("OK: [0x%08lX] = 0x%08lX\r\n", addr, data);
    } else {
        Serial.println(F("ERROR: Write failed"));
    }
}

static void cmd_erase(const char *args) {
    // Parse: <page> [bank]
    char page_str[20], bank_str[20];
    int n = sscanf(args, "%19s %19s", page_str, bank_str);

    if (n < 1) {
        Serial.println(F("Usage: erase <page> [bank]"));
        return;
    }

    uint8_t page = (uint8_t)parse_number(page_str);
    uint8_t bank = (n >= 2) ? (uint8_t)parse_number(bank_str) : 0;

    Serial.printf("Erasing bank %d page %d...\r\n", bank, page);

    if (stm32u5_flash_erase_page(bank, page)) {
        Serial.println(F("OK: Page erased"));
    } else {
        Serial.println(F("ERROR: Erase failed"));
    }
}

static void cmd_eraseall(void) {
    Serial.println(F("Mass erasing all flash (this may take up to 30 seconds)..."));

    if (stm32u5_flash_mass_erase()) {
        Serial.println(F("OK: Mass erase complete"));
    } else {
        Serial.println(F("ERROR: Mass erase failed"));
    }
}

static void cmd_unlock(void) {
    if (stm32u5_flash_unlock()) {
        Serial.println(F("OK: Flash unlocked"));
    } else {
        Serial.println(F("ERROR: Flash unlock failed"));
    }
}

static void cmd_lock(void) {
    if (stm32u5_flash_lock()) {
        Serial.println(F("OK: Flash locked"));
    } else {
        Serial.println(F("ERROR: Flash lock failed"));
    }
}

static void cmd_program(const char *args) {
    // Parse: <addr> <size>
    char addr_str[20], size_str[20];
    if (sscanf(args, "%19s %19s", addr_str, size_str) != 2) {
        Serial.println(F("Usage: program <addr> <size>"));
        return;
    }

    program_addr = parse_number(addr_str);
    program_size = parse_number(size_str);

    if (program_size == 0) {
        Serial.println(F("ERROR: Size must be > 0"));
        return;
    }

    if (program_addr & 0x0F) {
        Serial.println(F("ERROR: Address must be 16-byte aligned"));
        return;
    }

    Serial.printf("Programming %lu bytes at 0x%08lX\r\n", program_size, program_addr);

    // Step 1: Unlock flash
    Serial.println(F("Unlocking flash..."));
    if (!stm32u5_flash_unlock()) {
        Serial.println(F("ERROR: Flash unlock failed"));
        return;
    }

    // Step 2: Erase needed pages
    uint8_t start_page, page_count, bank;
    stm32u5_flash_calc_pages(program_addr, program_size, &start_page, &page_count, &bank);

    Serial.printf("Erasing %d pages (bank %d, starting page %d)...\r\n",
                  page_count, bank, start_page);

    for (uint8_t p = 0; p < page_count; p++) {
        uint8_t cur_page = start_page + p;
        uint8_t cur_bank = bank;

        // Handle crossing from bank 1 to bank 2
        if (cur_bank == 0 && cur_page >= FLASH_PAGES_PER_BANK) {
            cur_bank = 1;
            cur_page -= FLASH_PAGES_PER_BANK;
        }

        if (!stm32u5_flash_erase_page(cur_bank, cur_page)) {
            Serial.printf("ERROR: Failed to erase bank %d page %d\r\n", cur_bank, cur_page);
            stm32u5_flash_lock();
            return;
        }
        Serial.printf("  Erased page %d/%d\r\n", p + 1, page_count);
    }

    // Step 3: Enter binary receive mode
    program_received = 0;
    chunk_pos = 0;
    state = STATE_PROGRAMMING;
    Serial.println(F("READY"));  // Signal to host: start sending binary data
}

static void cmd_verify_readback(const char *args) {
    // Parse: <addr> <size>
    char addr_str[20], size_str[20];
    if (sscanf(args, "%19s %19s", addr_str, size_str) != 2) {
        Serial.println(F("Usage: verify <addr> <size>"));
        Serial.println(F("  After this command, send <size> bytes of expected data"));
        return;
    }

    uint32_t addr = parse_number(addr_str);
    uint32_t size = parse_number(size_str);

    Serial.printf("Verifying %lu bytes at 0x%08lX\r\n", size, addr);
    Serial.println(F("READY"));

    // Read expected data from serial and compare with flash
    uint32_t verified = 0;
    uint32_t mismatch_count = 0;

    while (verified < size) {
        // Wait for a chunk of data
        uint32_t chunk_size = min((uint32_t)FW_CHUNK_SIZE, size - verified);
        uint32_t received = 0;
        uint32_t timeout_start = millis();

        while (received < chunk_size) {
            if (Serial.available()) {
                fw_chunk[received++] = Serial.read();
                timeout_start = millis();
            } else if ((millis() - timeout_start) > 5000) {
                Serial.println(F("ERROR: Verify data timeout"));
                return;
            }
        }

        // Compare with flash
        uint32_t words = chunk_size / 4;
        uint32_t *expected = (uint32_t *)fw_chunk;

        for (uint32_t i = 0; i < words; i++) {
            uint32_t flash_val;
            if (!swd_mem_read32(addr + verified + i * 4, &flash_val)) {
                Serial.printf("ERROR: Read failed at 0x%08lX\r\n", addr + verified + i * 4);
                return;
            }
            if (flash_val != expected[i]) {
                mismatch_count++;
                if (mismatch_count <= 5) {
                    Serial.printf("MISMATCH at 0x%08lX: expected 0x%08lX got 0x%08lX\r\n",
                                  addr + verified + i * 4, expected[i], flash_val);
                }
            }
        }

        // Handle remaining bytes (less than 4)
        uint32_t remaining = chunk_size % 4;
        if (remaining > 0) {
            uint32_t flash_val;
            if (swd_mem_read32(addr + verified + words * 4, &flash_val)) {
                uint8_t *flash_bytes = (uint8_t *)&flash_val;
                for (uint32_t i = 0; i < remaining; i++) {
                    if (flash_bytes[i] != fw_chunk[words * 4 + i]) {
                        mismatch_count++;
                    }
                }
            }
        }

        verified += chunk_size;
    }

    if (mismatch_count == 0) {
        Serial.printf("OK: Verification passed (%lu bytes)\r\n", size);
    } else {
        Serial.printf("ERROR: %lu mismatches found\r\n", mismatch_count);
    }
}

static void cmd_status(void) {
    const char *state_names[] = {"IDLE", "CONNECTED", "PROGRAMMING", "ERROR"};
    Serial.printf("State: %s\r\n", state_names[state]);
    Serial.printf("GPIO: SWCLK=%d, SWDIO=%d, NRST=%d\r\n", SWCLK_PIN, SWDIO_PIN, NRST_PIN);

    if (state == STATE_CONNECTED || state == STATE_PROGRAMMING) {
        uint32_t idcode;
        if (swd_dp_read(DP_IDCODE, &idcode)) {
            Serial.printf("IDCODE: 0x%08lX\r\n", idcode);
        }

        uint32_t dhcsr;
        if (swd_mem_read32(DHCSR_ADDR, &dhcsr)) {
            Serial.printf("DHCSR: 0x%08lX (CPU %s)\r\n", dhcsr,
                          (dhcsr & DHCSR_S_HALT) ? "HALTED" : "RUNNING");
        }
    }
}

static void cmd_disconnect(void) {
    swd_unhalt_cpu();
    swd_reset_and_run();
    state = STATE_IDLE;
    Serial.println(F("OK: Disconnected"));
}

// ============================================================
// Command Dispatcher
// ============================================================

static void process_command(const char *cmd) {
    // Trim leading whitespace
    while (*cmd == ' ' || *cmd == '\t') cmd++;

    // Skip empty commands
    if (*cmd == '\0') return;

    // Find first space to separate command from arguments
    const char *args = cmd;
    while (*args && *args != ' ' && *args != '\t') args++;
    size_t cmd_len = args - cmd;

    // Skip whitespace to get to arguments
    while (*args == ' ' || *args == '\t') args++;

    // Match and dispatch commands
    if (strncasecmp(cmd, "help", cmd_len) == 0 || cmd[0] == '?') {
        cmd_help();
    }
    else if (strncasecmp(cmd, "connect", cmd_len) == 0) {
        cmd_connect();
    }
    else if (strncasecmp(cmd, "idcode", cmd_len) == 0) {
        cmd_idcode();
    }
    else if (strncasecmp(cmd, "halt", cmd_len) == 0) {
        cmd_halt();
    }
    else if (strncasecmp(cmd, "resume", cmd_len) == 0) {
        cmd_resume();
    }
    else if (strncasecmp(cmd, "reset", cmd_len) == 0) {
        cmd_reset();
    }
    else if (strncasecmp(cmd, "read", cmd_len) == 0) {
        cmd_read(args);
    }
    else if (strncasecmp(cmd, "write", cmd_len) == 0) {
        cmd_write(args);
    }
    else if (strncasecmp(cmd, "erase", cmd_len) == 0) {
        cmd_erase(args);
    }
    else if (strncasecmp(cmd, "eraseall", cmd_len) == 0) {
        cmd_eraseall();
    }
    else if (strncasecmp(cmd, "unlock", cmd_len) == 0) {
        cmd_unlock();
    }
    else if (strncasecmp(cmd, "lock", cmd_len) == 0) {
        cmd_lock();
    }
    else if (strncasecmp(cmd, "program", cmd_len) == 0) {
        cmd_program(args);
    }
    else if (strncasecmp(cmd, "verify", cmd_len) == 0) {
        cmd_verify_readback(args);
    }
    else if (strncasecmp(cmd, "status", cmd_len) == 0) {
        cmd_status();
    }
    else if (strncasecmp(cmd, "disconnect", cmd_len) == 0) {
        cmd_disconnect();
    }
    else {
        Serial.printf("Unknown command: '%s'. Type 'help' for commands.\r\n", cmd);
    }
}

// ============================================================
// Binary Upload Data Handler
// ============================================================

static void process_binary_data(void) {
    // In PROGRAMMING state, we receive raw binary firmware data
    while (Serial.available() && program_received < program_size) {
        fw_chunk[chunk_pos++] = Serial.read();
        program_received++;

        // When chunk is full or we've received everything, program it
        if (chunk_pos >= FW_CHUNK_SIZE || program_received >= program_size) {
            // Pad last chunk to 16-byte alignment if needed
            uint32_t write_len = chunk_pos;
            if (write_len & 0x0F) {
                uint32_t padded = (write_len + 15) & ~0x0F;
                memset(fw_chunk + write_len, 0xFF, padded - write_len);
                write_len = padded;
            }

            // Program this chunk
            uint32_t chunk_addr = program_addr + (program_received - chunk_pos);
            if (!stm32u5_flash_write(chunk_addr, fw_chunk, write_len)) {
                Serial.printf("ERROR: Programming failed at 0x%08lX\r\n", chunk_addr);
                stm32u5_flash_lock();
                state = STATE_ERROR;
                return;
            }

            // Progress update
            Serial.printf("PROGRESS: %lu / %lu bytes\r\n", program_received, program_size);
            chunk_pos = 0;
        }
    }

    // Check if upload is complete
    if (program_received >= program_size) {
        // Lock flash
        stm32u5_flash_lock();

        Serial.printf("OK: Programmed %lu bytes at 0x%08lX\r\n",
                      program_size, program_addr);
        state = STATE_CONNECTED;
    }
}

// ============================================================
// Public API
// ============================================================

void serial_cmd_init(void) {
    Serial.begin(SERIAL_BAUD);
    while (!Serial) {
        delay(10);
    }

    Serial.println();
    Serial.println(F("========================================"));
    Serial.println(F("  ESP32 SWD Programmer for STM32U585"));
    Serial.println(F("  Type 'help' for available commands"));
    Serial.println(F("========================================"));
    Serial.println();

    state = STATE_IDLE;
    cmd_pos = 0;
}

void serial_cmd_process(void) {
    // Handle binary upload mode separately
    if (state == STATE_PROGRAMMING) {
        process_binary_data();
        return;
    }

    // Text command mode: read characters until newline
    while (Serial.available()) {
        char c = Serial.read();

        if (c == '\r' || c == '\n') {
            if (cmd_pos > 0) {
                cmd_buffer[cmd_pos] = '\0';
                process_command(cmd_buffer);
                cmd_pos = 0;
            }
        }
        else if (c == '\b' || c == 0x7F) {
            // Backspace
            if (cmd_pos > 0) {
                cmd_pos--;
                Serial.print("\b \b");
            }
        }
        else if (cmd_pos < CMD_MAX_LEN - 1) {
            cmd_buffer[cmd_pos++] = c;
            Serial.print(c);  // Echo
        }
    }
}

ProgrammerState serial_cmd_get_state(void) {
    return state;
}
