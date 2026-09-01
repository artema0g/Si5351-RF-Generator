#include "si5351_ctrl.h"

Si5351Controller synth;

Si5351Controller::Si5351Controller()
    : _current_corr(0), _xtal_freq(SI5351_DEFAULT_XTAL_FREQ), _initialized(false)
{
    _freqs[0] = DEFAULT_CLK0_FREQ;
    _freqs[1] = DEFAULT_CLK1_FREQ;
    _freqs[2] = SI5351_FLL_REF_FREQ;
    
    _enabled[0] = true;
    _enabled[1] = false;
    _enabled[2] = true; // CLK2 enabled for FLL calibration
    
    _drives[0] = 3; // 8mA
    _drives[1] = 3; // 8mA
    _drives[2] = 1; // 4mA is sufficient for Arduino T1 counter input
}

bool Si5351Controller::begin(uint32_t xtal_freq, int32_t corr_ppb) {
    _xtal_freq = xtal_freq;
    _current_corr = corr_ppb;

    // Initialize Si5351: 10 pF crystal load capacitance, crystal frequency and correction in ppb
    bool success = _si5351.init(SI5351_CRYSTAL_LOAD_10PF, _xtal_freq, _current_corr);
    if (!success) {
        // Fallback to 8 pF load capacitance for modules with 8 pF crystals
        success = _si5351.init(SI5351_CRYSTAL_LOAD_8PF, _xtal_freq, _current_corr);
    }
    
    if (success) {
        _initialized = true;

        // Configure CLK0 (Primary output)
        setDriveStrength(0, _drives[0]);
        setFrequency(0, _freqs[0]);
        enableOutput(0, _enabled[0]);

        // Configure CLK1 (Auxiliary output)
        setDriveStrength(1, _drives[1]);
        setFrequency(1, _freqs[1]);
        enableOutput(1, _enabled[1]);

        // Configure CLK2 (FLL reference output for hardware counter D5)
        startFllRef(SI5351_FLL_REF_FREQ);
    }

    return success;
}

bool Si5351Controller::setFrequency(uint8_t clk, uint32_t freq_hz) {
    if (clk > 2) return false;
    if (freq_hz < 8000UL || freq_hz > 160000000UL) return false;

    _freqs[clk] = freq_hz;
    
    enum si5351_clock si_clk = (clk == 0) ? SI5351_CLK0 : ((clk == 1) ? SI5351_CLK1 : SI5351_CLK2);
    
    // In Etherkit library, frequency is set in centiHz (freq * 100ULL)
    uint8_t err = _si5351.set_freq((uint64_t)freq_hz * 100ULL, si_clk);
    return (err == 0);
}

void Si5351Controller::enableOutput(uint8_t clk, bool enable) {
    if (clk > 2) return;
    _enabled[clk] = enable;

    enum si5351_clock si_clk = (clk == 0) ? SI5351_CLK0 : ((clk == 1) ? SI5351_CLK1 : SI5351_CLK2);
    _si5351.output_enable(si_clk, enable ? 1 : 0);
}

void Si5351Controller::setDriveStrength(uint8_t clk, uint8_t drive_level) {
    if (clk > 2) return;
    _drives[clk] = drive_level & 0x03;

    enum si5351_clock si_clk = (clk == 0) ? SI5351_CLK0 : ((clk == 1) ? SI5351_CLK1 : SI5351_CLK2);
    enum si5351_drive drv = SI5351_DRIVE_8MA;
    switch (_drives[clk]) {
        case 0: drv = SI5351_DRIVE_2MA; break;
        case 1: drv = SI5351_DRIVE_4MA; break;
        case 2: drv = SI5351_DRIVE_6MA; break;
        case 3: drv = SI5351_DRIVE_8MA; break;
    }
    _si5351.drive_strength(si_clk, drv);
}

void Si5351Controller::setCorrection(int32_t corr_ppb) {
    _current_corr = corr_ppb;
    _si5351.set_correction(_current_corr, SI5351_PLL_INPUT_XO);
}

int32_t Si5351Controller::getCorrection() const {
    return _current_corr;
}

uint32_t Si5351Controller::getFrequency(uint8_t clk) const {
    return (clk <= 2) ? _freqs[clk] : 0UL;
}

bool Si5351Controller::isOutputEnabled(uint8_t clk) const {
    return (clk <= 2) ? _enabled[clk] : false;
}

uint8_t Si5351Controller::getDriveStrength(uint8_t clk) const {
    return (clk <= 2) ? _drives[clk] : 0;
}

void Si5351Controller::startFllRef(uint32_t ref_freq) {
    _freqs[2] = ref_freq;
    _enabled[2] = true;
    _drives[2] = 1; // 4mA is sufficient for Arduino D5 input

    _si5351.drive_strength(SI5351_CLK2, SI5351_DRIVE_4MA);
    _si5351.set_freq((uint64_t)ref_freq * 100ULL, SI5351_CLK2);
    _si5351.output_enable(SI5351_CLK2, 1);
}
