#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// ============================================================================
// ARDUINO NANO (ATmega328P) PIN ASSIGNMENTS
// ============================================================================
// D2 (INT0)  - 1PPS pulse input from NEO-6M GPS (Hardware interrupt)
// D3         - SoftwareSerial RX <- NEO-6M TX
// D4         - SoftwareSerial TX -> NEO-6M RX (optional)
// D5 (T1)    - Timer1 hardware counter input <- Si5351 CLK2 (1 MHz ref)
// A4 (SDA)   - I2C Data -> Si5351A SDA
// A5 (SCL)   - I2C Clock -> Si5351A SCL
// D13        - Built-in LED (1PPS pulse blink indicator)
// A0         - Optional frequency lock indicator LED (LOCK LED)
// ============================================================================

#define PIN_GPS_PPS      2   // 1PPS pulse input (INT0)
#define PIN_GPS_RX       3   // Arduino RX <- NEO-6M TX
#define PIN_GPS_TX       4   // Arduino TX -> NEO-6M RX
#define PIN_FLL_COUNTER  5   // Timer1 external clock input (T1) <- Si5351 CLK2
#define PIN_LED_PPS      13  // 1PPS / Status LED
#define PIN_LED_LOCK     A0  // Frequency lock LED

// ============================================================================
// COMMUNICATION SPEEDS
// ============================================================================
#define SERIAL_BAUD_RATE 115200  // USB Serial baud rate
#define GPS_BAUD_RATE    9600    // NEO-6M UART baud rate

// ============================================================================
// Si5351 SYNTHESIZER PARAMETERS
// ============================================================================
#define SI5351_DEFAULT_XTAL_FREQ 25000000UL // Crystal nominal frequency (25 MHz or 27 MHz)
#define SI5351_FLL_REF_FREQ      1000000UL  // CLK2 reference frequency for FLL: 1 MHz

// Default frequencies (in Hz, up to 160 MHz)
#define DEFAULT_CLK0_FREQ        10000000UL // CLK0: 10 MHz (Lab reference / generator)
#define DEFAULT_CLK1_FREQ        14074000UL // CLK1: 14.074 MHz (FT8 20m)

// ============================================================================
// FREQUENCY LOCKED LOOP (FLL) SETTINGS
// ============================================================================
#define FLL_GATE_SECONDS_FAST    1    // Fast initial step (1 sec)
#define FLL_GATE_SECONDS_NORM    10   // Normal tracking gate (10 sec -> 0.1 ppm / 100 ppb)
#define FLL_GATE_SECONDS_FINE    40   // Fine lock gate (40 sec -> 25 ppb)

#define FLL_MAX_CORR_PPB         200000L  // Maximum allowed correction (+/- 200 ppm / 200,000 ppb)
#define FLL_LOCK_THRESHOLD_PPB   150L     // Lock detection threshold (0.15 ppm / 150 ppb)

// ============================================================================
// EEPROM CONFIGURATION STRUCTURE
// ============================================================================
#define EEPROM_MAGIC 0x53495631 // Signature 'SIV1' (Si5351 V1)

struct DeviceConfig {
    uint32_t magic;           // Validation signature
    uint8_t  version;         // Struct version (1)
    uint32_t xtal_freq;       // Si5351 crystal frequency (25000000 or 27000000)
    int32_t  correction_ppb;  // Crystal correction in ppb (parts per billion)
    uint32_t clk0_freq;       // CLK0 frequency in Hz (up to 160 MHz)
    uint32_t clk1_freq;       // CLK1 frequency in Hz (up to 160 MHz)
    uint8_t  clk0_enabled;    // CLK0 state (1 - enabled, 0 - disabled)
    uint8_t  clk1_enabled;    // CLK1 state (1 - enabled, 0 - disabled)
    uint8_t  clk2_enabled;    // CLK2 state (1 - enabled, 0 - disabled)
    uint8_t  auto_discipline; // Continuous GPSDO disciplining mode
    uint8_t  clk0_drive;      // CLK0 drive level (0=2mA, 1=4mA, 2=6mA, 3=8mA)
    uint8_t  clk1_drive;      // CLK1 drive level
    uint8_t  checksum;        // XOR checksum
};

#endif // CONFIG_H
