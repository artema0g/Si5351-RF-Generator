/*
 * ============================================================================
 * PROJECT: Precision RF Generator / GPSDO (Si5351A + Arduino Nano + NEO-6M)
 * ============================================================================
 * 
 * Description:
 *   High-precision frequency synthesizer (8 kHz - 160 MHz) with automatic
 *   calibration and continuous disciplining against GPS 1PPS reference pulses.
 *   
 * Clock Outputs:
 *   - CLK0: Primary RF output (default: 10.000 000 MHz)
 *   - CLK1: Auxiliary RF output (default: 14.074 000 MHz)
 *   - CLK2: FLL reference clock 1.000 000 MHz (connected to pin D5)
 * 
 * Wiring to Arduino Nano:
 *   - Si5351 SDA  -> A4 (I2C SDA)
 *   - Si5351 SCL  -> A5 (I2C SCL)
 *   - Si5351 CLK2 -> D5 (Timer1 hardware counter input)
 *   - NEO-6M PPS  -> D2 (Hardware interrupt INT0)
 *   - NEO-6M TX   -> D3 (SoftwareSerial RX)
 *   - NEO-6M RX   -> D4 (SoftwareSerial TX, optional)
 *   - LED_LOCK    -> A0 (Optional frequency lock LED)
 *   - USB Serial  -> 115200 baud (Serial Monitor)
 * 
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "si5351_ctrl.h"
#include "gps_handler.h"
#include "fll_calibrator.h"
#include "cli_commands.h"

// Hardware interrupt handler for 1PPS pulse (Pin D2)
void ppsInterruptHandler() {
    fll.onPpsPulse();
    gpsHandler.notifyPpsInterrupt();
    digitalWrite(PIN_LED_PPS, HIGH);
}

void setup() {
    // Setup LEDs
    pinMode(PIN_LED_PPS, OUTPUT);
    digitalWrite(PIN_LED_PPS, LOW);

    // Start USB Serial Console (115200 baud)
    cli_begin();
    delay(200);

    Serial.println(F("\n=================================================="));
    Serial.println(F("   PRECISION RF GENERATOR Si5351A + GPS (NEO-6M)  "));
    Serial.println(F("               Arduino Nano (ATmega328P)          "));
    Serial.println(F("=================================================="));

    // Initialize I2C bus
    Wire.begin();
    Wire.setClock(400000); // 400 kHz Fast Mode for I2C

    // Load configuration from EEPROM
    config_load();

    // Initialize Si5351 clock generator
    Serial.print(F("[INIT] Initializing Si5351 (XTAL: "));
    Serial.print(currentConfig.xtal_freq / 1000000UL);
    Serial.println(F(" MHz)..."));
    
    if (synth.begin(currentConfig.xtal_freq, currentConfig.correction_ppb)) {
        Serial.println(F("[INIT] Si5351 detected successfully on I2C bus!"));
        
        // Restore channel configurations
        synth.setFrequency(0, currentConfig.clk0_freq);
        synth.setDriveStrength(0, currentConfig.clk0_drive);
        synth.enableOutput(0, currentConfig.clk0_enabled != 0);

        synth.setFrequency(1, currentConfig.clk1_freq);
        synth.setDriveStrength(1, currentConfig.clk1_drive);
        synth.enableOutput(1, currentConfig.clk1_enabled != 0);

        synth.enableOutput(2, currentConfig.clk2_enabled != 0);
    } else {
        Serial.println(F("[ERROR] Si5351 not found on I2C bus! Check wiring (A4/A5) and pullups."));
    }

    // Initialize NEO-6M GPS receiver
    Serial.println(F("[INIT] Starting GPS receiver (NEO-6M, 9600 baud)..."));
    gpsHandler.begin();

    // Initialize FLL hardware counter
    Serial.println(F("[INIT] Starting FLL frequency disciplining engine..."));
    fll.begin();
    fll.setAutoDiscipline(currentConfig.auto_discipline != 0);

    // Attach hardware interrupt for 1PPS on pin D2 (INT0)
    pinMode(PIN_GPS_PPS, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_GPS_PPS), ppsInterruptHandler, RISING);

    Serial.println(F("[INIT] System ready!"));
    Serial.println(F("Type 'help' or 'status' for commands."));
    Serial.println(F("--------------------------------------------------"));

    cli_print_status();
}

void loop() {
    // 1. Receive and parse GPS NMEA sentences
    gpsHandler.update();

    // 2. Process captured pulses and run FLL auto-disciplining
    fll.update();

    // 3. Process user CLI commands over Serial
    cli_update();

    // 4. Turn off 1PPS LED indicator 80ms after flash
    if (digitalRead(PIN_LED_PPS) == HIGH) {
        if (millis() - gpsHandler.getLastPpsMillis() > 80) {
            digitalWrite(PIN_LED_PPS, LOW);
        }
    }
}
