#include "fll_calibrator.h"
#include "si5351_ctrl.h"
#include "gps_handler.h"

FllCalibrator fll;

// Timer1 16-bit hardware overflow counter
volatile uint32_t timer1_overflow_count = 0;

// Interrupt on Timer1 overflow (input D5 / T1)
ISR(TIMER1_OVF_vect) {
    timer1_overflow_count++;
}

FllCalibrator::FllCalibrator()
    : _state(FLL_IDLE),
      _auto_discipline(true),
      _one_shot_active(false),
      _gate_target_seconds(FLL_GATE_SECONDS_NORM),
      _gate_current_second(0),
      _accum_ticks(0),
      _prev_capture_ticks(0),
      _last_error_ppb(0),
      _measured_freq_hz(SI5351_FLL_REF_FREQ),
      _isr_capture_ticks(0),
      _isr_pps_flag(false)
{
}

void FllCalibrator::begin() {
    pinMode(PIN_FLL_COUNTER, INPUT);
    pinMode(PIN_LED_LOCK, OUTPUT);
    digitalWrite(PIN_LED_LOCK, LOW);

    // Configure hardware 16-bit Timer1 to count external pulses on pin D5 (T1)
    noInterrupts();
    TCCR1A = 0;
    TCCR1B = 0; // Stop timer
    TCNT1 = 0;
    timer1_overflow_count = 0;
    TIFR1 = 0xFF; // Clear all pending interrupt flags on Timer1
    TIMSK1 = _BV(TOIE1); // Enable Timer1 overflow interrupt
    // CS12=1, CS11=1, CS10=1: clock on external pin T1 rising edge
    TCCR1B = _BV(CS12) | _BV(CS11) | _BV(CS10);
    interrupts();

    _state = FLL_IDLE;
}

void FllCalibrator::onPpsPulse() {
    // Atomic read of 16-bit TCNT1 and software overflow count
    uint16_t tcnt = TCNT1;
    uint32_t ovf = timer1_overflow_count;
    
    // Critical section handling: if counter overflowed just as interrupt fired
    if ((TIFR1 & _BV(TOV1)) && (tcnt < 0x8000)) {
        ovf++;
    }
    
    _isr_capture_ticks = (ovf << 16) | (uint32_t)tcnt;
    _isr_pps_flag = true;
}

void FllCalibrator::update() {
    if (!_isr_pps_flag) return;

    // Atomically grab captured tick count
    uint32_t current_ticks;
    noInterrupts();
    current_ticks = _isr_capture_ticks;
    _isr_pps_flag = false;
    interrupts();

    // Initial seed on first pulse
    if (_prev_capture_ticks == 0) {
        _prev_capture_ticks = current_ticks;
        return;
    }

    uint32_t elapsed_ticks = current_ticks - _prev_capture_ticks;
    _prev_capture_ticks = current_ticks;

    // Verify 1 MHz input presence on pin D5
    // Nominal count per 1 second is ~1,000,000 pulses
    if (elapsed_ticks < 500000UL || elapsed_ticks > 1500000UL) {
        _state = FLL_NO_SIGNAL;
        digitalWrite(PIN_LED_LOCK, LOW);
        _gate_current_second = 0;
        _accum_ticks = 0;
        return;
    }

    // Check GPS lock status (requires valid 3D fix)
    if (!gpsHandler.hasFix()) {
        _state = FLL_WAIT_GPS;
        digitalWrite(PIN_LED_LOCK, LOW);
        _gate_current_second = 0;
        _accum_ticks = 0;
        return;
    }

    // Process accumulation if auto-discipline or one-shot calibration is active
    if (_auto_discipline || _one_shot_active) {
        _accum_ticks += elapsed_ticks;
        _gate_current_second++;

        if (_state != FLL_LOCKED) {
            _state = FLL_CALIBRATING;
        }

        if (_gate_current_second >= _gate_target_seconds) {
            uint32_t expected_ticks = (uint32_t)SI5351_FLL_REF_FREQ * _gate_target_seconds;
            int32_t delta_ticks = (int32_t)(_accum_ticks - expected_ticks);
            
            processGateMeasurement(delta_ticks, _gate_target_seconds);

            _accum_ticks = 0;
            _gate_current_second = 0;
        }
    }
}

void FllCalibrator::processGateMeasurement(int32_t delta_ticks, uint8_t gate_sec) {
    _measured_freq_hz = (_accum_ticks + (gate_sec / 2)) / gate_sec;

    // Calculate fractional frequency offset in ppb:
    // delta_ppb = (delta_ticks / (1,000,000 * gate_sec)) * 1,000,000,000 = (delta_ticks * 1,000) / gate_sec
    int32_t delta_ppb = (delta_ticks * 1000L) / (int32_t)gate_sec;
    _last_error_ppb = delta_ppb;

    // Clamp maximum step correction for safety
    if (delta_ppb > FLL_MAX_CORR_PPB) delta_ppb = FLL_MAX_CORR_PPB;
    if (delta_ppb < -FLL_MAX_CORR_PPB) delta_ppb = -FLL_MAX_CORR_PPB;

    int32_t current_corr = synth.getCorrection();
    int32_t new_corr = current_corr + delta_ppb;

    // Clamp total correction range
    if (new_corr > FLL_MAX_CORR_PPB) new_corr = FLL_MAX_CORR_PPB;
    if (new_corr < -FLL_MAX_CORR_PPB) new_corr = -FLL_MAX_CORR_PPB;

    // Apply updated correction into Si5351
    synth.setCorrection(new_corr);

    // Check lock threshold
    if (abs(delta_ppb) <= FLL_LOCK_THRESHOLD_PPB) {
        _state = FLL_LOCKED;
        digitalWrite(PIN_LED_LOCK, HIGH);
        // Once locked, increase integration gate to 40s for ultra-fine tracking
        _gate_target_seconds = FLL_GATE_SECONDS_FINE;
    } else {
        _state = FLL_CALIBRATING;
        digitalWrite(PIN_LED_LOCK, LOW);
        // If drifted outside threshold, return to 10s gate
        _gate_target_seconds = FLL_GATE_SECONDS_NORM;
    }

    if (_one_shot_active) {
        _one_shot_active = false;
        Serial.println();
        Serial.print(F("[CAL] Measurement complete! Measured: "));
        Serial.print(_measured_freq_hz);
        Serial.print(F(" Hz (error: "));
        Serial.print(delta_ppb);
        Serial.print(F(" ppb). New Si5351 correction: "));
        Serial.print(new_corr);
        Serial.println(F(" ppb."));
        Serial.println(F("[CAL] Type 'save' to store into EEPROM."));
    }
}

void FllCalibrator::triggerOneShotCalibration(uint8_t gate_seconds) {
    _one_shot_active = true;
    _gate_target_seconds = gate_seconds;
    _gate_current_second = 0;
    _accum_ticks = 0;
    Serial.println();
    Serial.print(F("[CAL] Starting calibration (gate: "));
    Serial.print(gate_seconds);
    Serial.println(F(" sec)..."));
}

void FllCalibrator::setAutoDiscipline(bool enable) {
    _auto_discipline = enable;
    if (!enable) {
        digitalWrite(PIN_LED_LOCK, LOW);
        _state = FLL_IDLE;
    }
}

bool FllCalibrator::isAutoDiscipline() const {
    return _auto_discipline;
}

FllState FllCalibrator::getStatus() const {
    return _state;
}

const char* FllCalibrator::getStatusStr() const {
    switch (_state) {
        case FLL_IDLE:        return "IDLE (Disabled)";
        case FLL_NO_SIGNAL:   return "NO SIGNAL ON D5 (CLK2?)";
        case FLL_WAIT_GPS:    return "WAITING FOR GPS FIX";
        case FLL_CALIBRATING: return "CALIBRATING / TUNING";
        case FLL_LOCKED:      return "LOCKED (Disciplined)";
        default:              return "UNKNOWN";
    }
}

int32_t FllCalibrator::getLastErrorPpb() const {
    return _last_error_ppb;
}

uint32_t FllCalibrator::getMeasuredFreqHz() const {
    return _measured_freq_hz;
}

uint32_t FllCalibrator::getGateElapsedSec() const {
    return _gate_current_second;
}

uint32_t FllCalibrator::getGateTargetSec() const {
    return _gate_target_seconds;
}

bool FllCalibrator::isOneShotActive() const {
    return _one_shot_active;
}
