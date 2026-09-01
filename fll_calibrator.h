#ifndef FLL_CALIBRATOR_H
#define FLL_CALIBRATOR_H

#include <Arduino.h>
#include "config.h"

enum FllState {
    FLL_IDLE,        // Idle / waiting to start
    FLL_NO_SIGNAL,   // No input signal detected from CLK2 on pin D5
    FLL_WAIT_GPS,    // Waiting for stable GPS 3D fix
    FLL_CALIBRATING, // Pulse counting and tuning in progress
    FLL_LOCKED       // Frequency locked (error within threshold)
};

class FllCalibrator {
public:
    FllCalibrator();

    void begin();
    void update(); // Call regularly in loop()

    // 1PPS interrupt callback (called from ISR)
    void onPpsPulse();

    // Trigger immediate one-shot calibration cycle
    void triggerOneShotCalibration(uint8_t gate_seconds = FLL_GATE_SECONDS_NORM);

    // Toggle continuous GPSDO auto-disciplining
    void setAutoDiscipline(bool enable);
    bool isAutoDiscipline() const;

    FllState getStatus() const;
    const char* getStatusStr() const;

    int32_t  getLastErrorPpb() const;
    uint32_t getMeasuredFreqHz() const;
    uint32_t getGateElapsedSec() const;
    uint32_t getGateTargetSec() const;
    bool     isOneShotActive() const;

private:
    void processGateMeasurement(int32_t delta_ticks, uint8_t gate_sec);

    FllState _state;
    bool     _auto_discipline;
    bool     _one_shot_active;

    uint8_t  _gate_target_seconds;
    uint8_t  _gate_current_second;
    uint32_t _accum_ticks;

    uint32_t _prev_capture_ticks;
    int32_t  _last_error_ppb;
    uint32_t _measured_freq_hz;

    volatile uint32_t _isr_capture_ticks;
    volatile bool     _isr_pps_flag;
};

extern FllCalibrator fll;

#endif // FLL_CALIBRATOR_H
