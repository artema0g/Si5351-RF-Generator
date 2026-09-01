#include "cli_commands.h"
#include "si5351_ctrl.h"
#include "gps_handler.h"
#include "fll_calibrator.h"

DeviceConfig currentConfig;

static char inputBuffer[64];
static uint8_t inputPos = 0;

static uint8_t calculate_checksum(const DeviceConfig& cfg) {
    const uint8_t* ptr = (const uint8_t*)&cfg;
    uint8_t crc = 0xAA;
    size_t len = sizeof(DeviceConfig) - sizeof(cfg.checksum);
    for (size_t i = 0; i < len; i++) {
        crc ^= ptr[i];
    }
    return crc;
}

void config_reset_defaults() {
    currentConfig.magic = EEPROM_MAGIC;
    currentConfig.version = 1;
    currentConfig.xtal_freq = SI5351_DEFAULT_XTAL_FREQ;
    currentConfig.correction_ppb = 0;
    currentConfig.clk0_freq = DEFAULT_CLK0_FREQ;
    currentConfig.clk1_freq = DEFAULT_CLK1_FREQ;
    currentConfig.clk0_enabled = 1;
    currentConfig.clk1_enabled = 0;
    currentConfig.clk2_enabled = 1;
    currentConfig.auto_discipline = 1;
    currentConfig.clk0_drive = 3; // 8mA
    currentConfig.clk1_drive = 3; // 8mA
    currentConfig.checksum = calculate_checksum(currentConfig);
}

void config_load() {
    DeviceConfig stored;
    EEPROM.get(0, stored);

    if (stored.magic == EEPROM_MAGIC && stored.version == 1 &&
        stored.checksum == calculate_checksum(stored)) {
        currentConfig = stored;
        Serial.println(F("[EEPROM] Configuration loaded successfully."));
    } else {
        Serial.println(F("[EEPROM] Valid configuration not found. Loading defaults."));
        config_reset_defaults();
        config_save();
    }
}

void config_save() {
    currentConfig.correction_ppb = synth.getCorrection();
    currentConfig.clk0_freq = synth.getFrequency(0);
    currentConfig.clk1_freq = synth.getFrequency(1);
    currentConfig.clk0_enabled = synth.isOutputEnabled(0) ? 1 : 0;
    currentConfig.clk1_enabled = synth.isOutputEnabled(1) ? 1 : 0;
    currentConfig.clk2_enabled = synth.isOutputEnabled(2) ? 1 : 0;
    currentConfig.auto_discipline = fll.isAutoDiscipline() ? 1 : 0;
    currentConfig.clk0_drive = synth.getDriveStrength(0);
    currentConfig.clk1_drive = synth.getDriveStrength(1);
    currentConfig.checksum = calculate_checksum(currentConfig);

    EEPROM.put(0, currentConfig);
    Serial.println(F("[EEPROM] Configuration saved to EEPROM."));
}

static void print_freq_formatted(uint32_t freq_hz) {
    uint32_t mhz = freq_hz / 1000000UL;
    uint32_t khz = (freq_hz % 1000000UL) / 1000UL;
    uint16_t hz  = (uint16_t)(freq_hz % 1000UL);

    if (mhz > 0) {
        Serial.print(mhz);
        Serial.print(F("."));
        if (khz < 100) Serial.print(F("0"));
        if (khz < 10)  Serial.print(F("0"));
        Serial.print(khz);
        Serial.print(F(" "));
        if (hz < 100) Serial.print(F("0"));
        if (hz < 10)  Serial.print(F("0"));
        Serial.print(hz);
        Serial.print(F(" MHz"));
    } else {
        Serial.print(freq_hz);
        Serial.print(F(" Hz"));
    }
}

void cli_print_status() {
    char timeBuf[12];
    char dateBuf[14];
    char gridBuf[8];
    gpsHandler.getTimeStr(timeBuf, sizeof(timeBuf));
    gpsHandler.getDateStr(dateBuf, sizeof(dateBuf));
    gpsHandler.getGridLocator(gridBuf, sizeof(gridBuf));

    Serial.println(F("\n================= SYSTEM STATUS ================="));
    
    // GPS
    Serial.print(F("GPS Status     : "));
    if (gpsHandler.hasFix()) {
        Serial.print(F("3D FIX (LOCKED) | Sats: "));
        Serial.print(gpsHandler.getSatellites());
        Serial.print(F(" | HDOP: "));
        uint16_t hd = gpsHandler.getHdop100();
        Serial.print(hd / 100);
        Serial.print(F("."));
        uint8_t hdRem = (hd % 100) / 10;
        Serial.println(hdRem);
    } else {
        Serial.print(F("NO FIX (SEARCHING) | Sats: "));
        Serial.println(gpsHandler.getSatellites());
    }
    Serial.print(F("UTC Time/Date  : "));
    Serial.print(timeBuf);
    Serial.print(F("  "));
    Serial.print(dateBuf);
    Serial.print(F("  QTH: "));
    Serial.println(gridBuf);

    Serial.print(F("1PPS Signal    : "));
    if (gpsHandler.hasPps()) {
        Serial.print(F("ACTIVE (Pulses: "));
        Serial.print(gpsHandler.getPpsCount());
        Serial.println(F(")"));
    } else {
        Serial.println(F("NO PULSES (Check D2)"));
    }

    // FLL / Calibration
    Serial.println(F("-------------------------------------------------"));
    Serial.print(F("FLL Engine     : "));
    Serial.print(fll.getStatusStr());
    Serial.print(F(" | Auto-discipline: "));
    Serial.println(fll.isAutoDiscipline() ? F("ON (GPSDO)") : F("OFF"));
    
    Serial.print(F("Si5351 XTAL    : "));
    Serial.print(currentConfig.xtal_freq / 1000000UL);
    Serial.print(F(" MHz | Current correction: "));
    int32_t corr = synth.getCorrection();
    Serial.print(corr);
    Serial.print(F(" ppb ("));
    Serial.print(corr / 1000);
    Serial.print(F("."));
    int16_t p_rem = abs((int16_t)(corr % 1000));
    if (p_rem < 100) Serial.print(F("0"));
    if (p_rem < 10)  Serial.print(F("0"));
    Serial.print(p_rem);
    Serial.println(F(" ppm)"));

    Serial.print(F("FLL Measured   : "));
    Serial.print(fll.getMeasuredFreqHz());
    Serial.print(F(" Hz (error: "));
    Serial.print(fll.getLastErrorPpb());
    Serial.print(F(" ppb) [Gate: "));
    Serial.print(fll.getGateElapsedSec());
    Serial.print(F("/"));
    Serial.print(fll.getGateTargetSec());
    Serial.println(F(" s]"));

    // Outputs
    Serial.println(F("-------------------------------------------------"));
    Serial.print(F("CLK0 (Primary) : ["));
    Serial.print(synth.isOutputEnabled(0) ? F("ON") : F("OFF"));
    Serial.print(F("] "));
    print_freq_formatted(synth.getFrequency(0));
    Serial.print(F(" (Drive: "));
    Serial.print((synth.getDriveStrength(0) + 1) * 2);
    Serial.println(F("mA)"));

    Serial.print(F("CLK1 (Aux)     : ["));
    Serial.print(synth.isOutputEnabled(1) ? F("ON") : F("OFF"));
    Serial.print(F("] "));
    print_freq_formatted(synth.getFrequency(1));
    Serial.print(F(" (Drive: "));
    Serial.print((synth.getDriveStrength(1) + 1) * 2);
    Serial.println(F("mA)"));

    Serial.print(F("CLK2 (FLL Ref) : ["));
    Serial.print(synth.isOutputEnabled(2) ? F("ON") : F("OFF"));
    Serial.print(F("] 1.000 000 MHz -> D5 Counter Input"));
    Serial.println();
    Serial.println(F("================================================="));
}

void cli_print_help() {
    Serial.println(F("\n================ AVAILABLE COMMANDS ================"));
    Serial.println(F("status            - Print current system status"));
    Serial.println(F("freq <clk> <hz>   - Set frequency in Hz (e.g. freq 0 10000000)"));
    Serial.println(F("out <clk> <on|off>- Enable/disable output (e.g. out 0 on)"));
    Serial.println(F("drive <clk> <mA>  - Output drive 2, 4, 6 or 8 mA (e.g. drive 0 8)"));
    Serial.println(F("cal [sec]         - Run one-shot calibration (e.g. cal 10)"));
    Serial.println(F("fll <on|off>      - Toggle continuous GPSDO auto-discipline"));
    Serial.println(F("corr <ppb>        - Manually set crystal correction in ppb"));
    Serial.println(F("xtal <hz>         - Set crystal nominal frequency (25000000/27000000)"));
    Serial.println(F("save              - Save configuration to EEPROM"));
    Serial.println(F("load              - Reload configuration from EEPROM"));
    Serial.println(F("reset             - Reset configuration to factory defaults"));
    Serial.println(F("help, ?           - Print this command list"));
    Serial.println(F("===================================================="));
}

static void handle_command(char* cmdLine) {
    while (*cmdLine == ' ') cmdLine++;
    if (*cmdLine == 0) return;

    char* cmd = strtok(cmdLine, " ");
    if (!cmd) return;

    for (char* p = cmd; *p; ++p) *p = tolower(*p);

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cli_print_help();
    }
    else if (strcmp(cmd, "status") == 0) {
        cli_print_status();
    }
    else if (strcmp(cmd, "freq") == 0) {
        char* clkStr = strtok(NULL, " ");
        char* freqStr = strtok(NULL, " ");
        if (!clkStr || !freqStr) {
            Serial.println(F("Error! Usage: freq <0|1> <frequency_hz>"));
            return;
        }
        uint8_t clk = atoi(clkStr);
        uint32_t freq = strtoul(freqStr, NULL, 10);
        if (clk > 1) {
            Serial.println(F("Error! Channels 0 and 1 only (channel 2 is reserved for FLL)."));
            return;
        }
        if (freq < 8000UL || freq > 160000000UL) {
            Serial.println(F("Error! Frequency must be between 8000 and 160000000 Hz."));
            return;
        }
        if (synth.setFrequency(clk, freq)) {
            Serial.print(F("Channel CLK"));
            Serial.print(clk);
            Serial.print(F(" set to "));
            print_freq_formatted(freq);
            Serial.println();
        } else {
            Serial.println(F("Error configuring frequency!"));
        }
    }
    else if (strcmp(cmd, "out") == 0) {
        char* clkStr = strtok(NULL, " ");
        char* stateStr = strtok(NULL, " ");
        if (!clkStr || !stateStr) {
            Serial.println(F("Error! Usage: out <0|1|2> <on|off|1|0>"));
            return;
        }
        uint8_t clk = atoi(clkStr);
        for (char* p = stateStr; *p; ++p) *p = tolower(*p);
        bool en = (strcmp(stateStr, "on") == 0 || strcmp(stateStr, "1") == 0);
        synth.enableOutput(clk, en);
        Serial.print(F("Channel CLK"));
        Serial.print(clk);
        Serial.println(en ? F(" ENABLED") : F(" DISABLED"));
    }
    else if (strcmp(cmd, "drive") == 0) {
        char* clkStr = strtok(NULL, " ");
        char* mAStr = strtok(NULL, " ");
        if (!clkStr || !mAStr) {
            Serial.println(F("Error! Usage: drive <0|1> <2|4|6|8>"));
            return;
        }
        uint8_t clk = atoi(clkStr);
        uint8_t mA = atoi(mAStr);
        uint8_t drv = 3;
        if (mA == 2) drv = 0;
        else if (mA == 4) drv = 1;
        else if (mA == 6) drv = 2;
        else if (mA == 8) drv = 3;
        else {
            Serial.println(F("Error! Valid drive currents: 2, 4, 6, 8 mA."));
            return;
        }
        synth.setDriveStrength(clk, drv);
        Serial.print(F("Channel CLK"));
        Serial.print(clk);
        Serial.print(F(" drive current set to "));
        Serial.print(mA);
        Serial.println(F(" mA"));
    }
    else if (strcmp(cmd, "cal") == 0) {
        char* secStr = strtok(NULL, " ");
        uint8_t sec = secStr ? atoi(secStr) : FLL_GATE_SECONDS_NORM;
        if (sec < 2 || sec > 60) sec = FLL_GATE_SECONDS_NORM;
        fll.triggerOneShotCalibration(sec);
    }
    else if (strcmp(cmd, "fll") == 0) {
        char* stateStr = strtok(NULL, " ");
        if (!stateStr) {
            Serial.println(F("Error! Usage: fll <on|off>"));
            return;
        }
        for (char* p = stateStr; *p; ++p) *p = tolower(*p);
        bool en = (strcmp(stateStr, "on") == 0 || strcmp(stateStr, "1") == 0);
        fll.setAutoDiscipline(en);
        Serial.print(F("Continuous FLL GPSDO disciplining: "));
        Serial.println(en ? F("ENABLED") : F("DISABLED"));
    }
    else if (strcmp(cmd, "corr") == 0) {
        char* corrStr = strtok(NULL, " ");
        if (!corrStr) {
            Serial.print(F("Current correction: "));
            Serial.print(synth.getCorrection());
            Serial.println(F(" ppb"));
            return;
        }
        int32_t corr = atol(corrStr);
        synth.setCorrection(corr);
        Serial.print(F("Correction set to: "));
        Serial.print(corr);
        Serial.println(F(" ppb"));
    }
    else if (strcmp(cmd, "xtal") == 0) {
        char* xtalStr = strtok(NULL, " ");
        if (!xtalStr) {
            Serial.print(F("Current XTAL frequency: "));
            Serial.print(currentConfig.xtal_freq);
            Serial.println(F(" Hz"));
            return;
        }
        uint32_t xf = strtoul(xtalStr, NULL, 10);
        if (xf != 25000000UL && xf != 27000000UL) {
            Serial.println(F("Notice: Common XTAL frequencies are 25000000 or 27000000 Hz."));
        }
        currentConfig.xtal_freq = xf;
        synth.begin(xf, synth.getCorrection());
        Serial.print(F("XTAL frequency updated to: "));
        Serial.print(xf);
        Serial.println(F(" Hz"));
    }
    else if (strcmp(cmd, "save") == 0) {
        config_save();
    }
    else if (strcmp(cmd, "load") == 0) {
        config_load();
        synth.begin(currentConfig.xtal_freq, currentConfig.correction_ppb);
        synth.setFrequency(0, currentConfig.clk0_freq);
        synth.setFrequency(1, currentConfig.clk1_freq);
        synth.enableOutput(0, currentConfig.clk0_enabled != 0);
        synth.enableOutput(1, currentConfig.clk1_enabled != 0);
        fll.setAutoDiscipline(currentConfig.auto_discipline != 0);
        cli_print_status();
    }
    else if (strcmp(cmd, "reset") == 0) {
        config_reset_defaults();
        config_save();
        Serial.println(F("Configuration reset to factory defaults."));
        cli_print_status();
    }
    else {
        Serial.print(F("Unknown command: "));
        Serial.println(cmd);
        Serial.println(F("Type 'help' for available commands."));
    }
}

void cli_begin() {
    Serial.begin(SERIAL_BAUD_RATE);
    inputPos = 0;
}

void cli_update() {
    while (Serial.available() > 0) {
        char c = (char)Serial.read();
        if (c == '\r' || c == '\n') {
            if (inputPos > 0) {
                inputBuffer[inputPos] = 0;
                handle_command(inputBuffer);
                inputPos = 0;
            }
        } else if (c == '\b' || c == 127) {
            if (inputPos > 0) {
                inputPos--;
            }
        } else if (inputPos < sizeof(inputBuffer) - 1) {
            inputBuffer[inputPos++] = c;
        }
    }
}
