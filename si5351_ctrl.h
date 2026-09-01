#ifndef SI5351_CTRL_H
#define SI5351_CTRL_H

#include <Arduino.h>
#include <si5351.h>
#include "config.h"

class Si5351Controller {
public:
    Si5351Controller();

    bool begin(uint32_t xtal_freq, int32_t corr_ppb);
    bool setFrequency(uint8_t clk, uint32_t freq_hz);
    void enableOutput(uint8_t clk, bool enable);
    void setDriveStrength(uint8_t clk, uint8_t drive_level); // 0=2mA, 1=4mA, 2=6mA, 3=8mA
    void setCorrection(int32_t corr_ppb);
    
    int32_t  getCorrection() const;
    uint32_t getFrequency(uint8_t clk) const;
    bool     isOutputEnabled(uint8_t clk) const;
    uint8_t  getDriveStrength(uint8_t clk) const;
    
    // Configure CLK2 as reference output for FLL feedback (1 MHz)
    void startFllRef(uint32_t ref_freq = SI5351_FLL_REF_FREQ);

    Si5351& getRawDriver() { return _si5351; }

private:
    Si5351   _si5351;
    uint32_t _freqs[3];
    bool     _enabled[3];
    uint8_t  _drives[3];
    int32_t  _current_corr;
    uint32_t _xtal_freq;
    bool     _initialized;
};

extern Si5351Controller synth;

#endif // SI5351_CTRL_H
