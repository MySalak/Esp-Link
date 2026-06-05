#include "swd_bitbang.h"
#include "pin_config.h"
#include <Arduino.h>
#include "driver/gpio.h"

// ============================================================
// Fast GPIO Macros (ESP32 direct register access)
// These are much faster than Arduino's digitalWrite/digitalRead
// Only works for GPIO 0-31 (our pins 19, 21, 22 qualify)
// ============================================================

#define SWCLK_SET()     GPIO.out_w1ts = (1UL << SWCLK_PIN)
#define SWCLK_CLR()     GPIO.out_w1tc = (1UL << SWCLK_PIN)
#define SWDIO_SET()     GPIO.out_w1ts = (1UL << SWDIO_PIN)
#define SWDIO_CLR()     GPIO.out_w1tc = (1UL << SWDIO_PIN)
#define SWDIO_READ()    ((GPIO.in >> SWDIO_PIN) & 1)

// ============================================================
// SWDIO Direction Switching
// Using GPIO enable register for fast direction changes.
// When output is disabled, the pin acts as input (with pull-up).
// ============================================================

static inline void swdio_output_enable(void) {
    GPIO.enable_w1ts = (1UL << SWDIO_PIN);
}

static inline void swdio_input_enable(void) {
    GPIO.enable_w1tc = (1UL << SWDIO_PIN);
}

// ============================================================
// Small delay for SWD clock timing
// At 240MHz ESP32, each NOP ~4ns. 4 NOPs ≈ 16ns.
// Total clock period ≈ 2 × (setup + delay) ≈ ~500kHz–1MHz
// Increase SWD_DELAY_CYCLES in pin_config.h if unreliable.
// ============================================================

// Compile-time unrolled NOP delay — zero loop overhead.
// The compiler sees a fixed number of asm nops and inlines them directly.
template<int N> static inline void swd_delay_nops() {
    swd_delay_nops<N-1>();
    __asm__ __volatile__("nop");
}
template<> inline void swd_delay_nops<0>() {}

static inline void swd_delay(void) {
    swd_delay_nops<SWD_DELAY_CYCLES>();
}

// ============================================================
// Bit-level I/O
// SWD data is driven/sampled on the RISING edge of SWCLK.
// ============================================================

/// Write one bit on SWDIO, clocking SWCLK low-then-high
static void swd_write_bit(uint8_t bit) {
    if (bit) {
        SWDIO_SET();
    } else {
        SWDIO_CLR();
    }
    swd_delay();
    SWCLK_CLR();
    swd_delay();
    SWCLK_SET();
    swd_delay();
}

/// Read one bit from SWDIO on rising edge of SWCLK
static uint8_t swd_read_bit(void) {
    SWCLK_CLR();
    swd_delay();
    uint8_t bit = SWDIO_READ();
    SWCLK_SET();
    swd_delay();
    return bit;
}

/// Clock one turnaround cycle (SWDIO not driven/read)
static void swd_clock_cycle(void) {
    SWCLK_CLR();
    swd_delay();
    SWCLK_SET();
    swd_delay();
}

// ============================================================
// Public Functions
// ============================================================

void swd_init(void) {
    // Configure SWCLK and NRST using Arduino API
    pinMode(SWCLK_PIN, OUTPUT);
    
    // NRST is configured as open-drain so that if a physical reset button on the board
    // pulls NRST to GND, it does not cause a short circuit with the ESP32 output driver.
    pinMode(NRST_PIN, OUTPUT_OPEN_DRAIN);

    // Initialize SWDIO using Arduino API FIRST so the GPIO matrix routes the signal to the pad correctly.
    pinMode(SWDIO_PIN, OUTPUT);
    
    // Then override the pad configuration using ESP-IDF to ensure BOTH input and output buffers are enabled.
    // This allows us to use ultra-fast GPIO.enable_w1ts / w1tc for direction switching.
    gpio_set_direction((gpio_num_t)SWDIO_PIN, GPIO_MODE_INPUT_OUTPUT);
    gpio_set_pull_mode((gpio_num_t)SWDIO_PIN, GPIO_PULLUP_ONLY);

    // Default states
    digitalWrite(SWCLK_PIN, HIGH);
    
    // Enable SWDIO output driver by default and drive it HIGH
    GPIO.enable_w1ts = (1UL << SWDIO_PIN);
    digitalWrite(SWDIO_PIN, HIGH);
    
    digitalWrite(NRST_PIN, HIGH);  // NRST deasserted (target not in reset)
}

void swd_line_reset(void) {
    // Drive SWDIO HIGH for at least 50 clock cycles
    // This resets the SWD state machine in the target
    swdio_output_enable();
    SWDIO_SET();

    for (int i = 0; i < 56; i++) {
        SWCLK_CLR();
        swd_delay();
        SWCLK_SET();
        swd_delay();
    }

    // Two idle cycles (SWDIO LOW) to complete the reset
    SWDIO_CLR();
    swd_clock_cycle();
    swd_clock_cycle();
}

void swd_jtag_to_swd(void) {
    swdio_output_enable();

    // Step 1: Line reset (≥50 clocks with SWDIO HIGH)
    // We cannot use swd_line_reset() here because it appends LOW idle cycles,
    // which violates the ARM ADIv5 specification for JTAG-to-SWD switching.
    SWDIO_SET();
    for (int i = 0; i < 56; i++) {
        SWCLK_CLR();
        swd_delay();
        SWCLK_SET();
        swd_delay();
    }

    // Step 2: Send 16-bit JTAG-to-SWD switching sequence
    // 0xE79E transmitted LSB first
    uint16_t seq = 0xE79E;
    for (int i = 0; i < 16; i++) {
        swd_write_bit((seq >> i) & 1);
    }

    // Step 3: Another line reset (≥50 clocks with SWDIO HIGH)
    SWDIO_SET();
    for (int i = 0; i < 56; i++) {
        SWCLK_CLR();
        swd_delay();
        SWCLK_SET();
        swd_delay();
    }

    // Step 4: Idle cycles (must be at least 2)
    SWDIO_CLR();
    for (int i = 0; i < 4; i++) {
        swd_clock_cycle();
    }
}

uint8_t swd_build_request(uint8_t APnDP, uint8_t RnW, uint8_t addr) {
    uint8_t request = 0;

    // Bit 0: Start bit = 1
    request |= (1 << 0);

    // Bit 1: APnDP (0 = Debug Port, 1 = Access Port)
    request |= ((APnDP & 1) << 1);

    // Bit 2: RnW (0 = Write, 1 = Read)
    request |= ((RnW & 1) << 2);

    // Bits 3-4: A[2:3] from the register address
    // addr bits [3:2] map to request bits [4:3]
    uint8_t a2 = (addr >> 2) & 1;
    uint8_t a3 = (addr >> 3) & 1;
    request |= (a2 << 3);
    request |= (a3 << 4);

    // Bit 5: Even parity over bits 1-4
    uint8_t parity = ((request >> 1) & 1) ^
                     ((request >> 2) & 1) ^
                     ((request >> 3) & 1) ^
                     ((request >> 4) & 1);
    request |= (parity << 5);

    // Bit 6: Stop = 0 (already 0)
    // Bit 7: Park = 1
    request |= (1 << 7);

    return request;
}

uint8_t swd_transfer(uint8_t request, uint32_t *data) {
    uint8_t ack = 0;
    uint32_t val = 0;
    uint8_t parity_calc = 0;
    uint8_t bit;

    // -------------------------------------------------------
    // Phase 1: Send 8-bit request header (LSB first)
    // -------------------------------------------------------
    swdio_output_enable();
    for (int i = 0; i < 8; i++) {
        swd_write_bit((request >> i) & 1);
    }

    // -------------------------------------------------------
    // Turnaround: Switch SWDIO to input, clock once
    // Host releases SWDIO, target takes over
    // -------------------------------------------------------
    swdio_input_enable();
    swd_clock_cycle();

    // -------------------------------------------------------
    // Phase 2: Read 3-bit ACK (LSB first)
    // -------------------------------------------------------
    for (int i = 0; i < 3; i++) {
        bit = swd_read_bit();
        ack |= (bit << i);
    }

    // -------------------------------------------------------
    // Phase 3: Data transfer (only if ACK == OK)
    // -------------------------------------------------------
    if (ack == SWD_ACK_OK) {
        if (request & (1 << 2)) {
            // --- READ transfer ---
            // Target drives 32-bit data + 1-bit parity
            val = 0;
            parity_calc = 0;
            for (int i = 0; i < 32; i++) {
                bit = swd_read_bit();
                val |= ((uint32_t)bit << i);
                parity_calc ^= bit;
            }

            // Read parity bit from target
            uint8_t parity_received = swd_read_bit();

            // Turnaround: target releases, host takes over
            swdio_output_enable();
            swd_clock_cycle();

            // Check parity
            if (parity_calc != parity_received) {
                // Parity error — treat as protocol error
                if (data) *data = val;
                return SWD_ACK_NONE;
            }

            if (data) *data = val;

        } else {
            // --- WRITE transfer ---
            // Turnaround: target releases SWDIO, host takes over
            swdio_output_enable();
            swd_clock_cycle();

            // Host drives 32-bit data (LSB first)
            val = data ? *data : 0;
            parity_calc = 0;
            for (int i = 0; i < 32; i++) {
                bit = (val >> i) & 1;
                swd_write_bit(bit);
                parity_calc ^= bit;
            }

            // Write parity bit
            swd_write_bit(parity_calc);
        }
    } else {
        // WAIT, FAULT, or no response — release and go back to output
        swdio_output_enable();
    }

    // -------------------------------------------------------
    // Idle cycles: drive SWDIO LOW for 8 clocks
    // This ensures the target completes the transaction
    // -------------------------------------------------------
    swdio_output_enable();
    SWDIO_CLR();
    for (int i = 0; i < 8; i++) {
        swd_clock_cycle();
    }

    return ack;
}

void swd_target_reset_assert(void) {
    digitalWrite(NRST_PIN, LOW);
}

void swd_target_reset_deassert(void) {
    digitalWrite(NRST_PIN, HIGH);
}
