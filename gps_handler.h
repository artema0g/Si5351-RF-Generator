#ifndef GPS_HANDLER_H
#define GPS_HANDLER_H

#include <Arduino.h>
#include <SoftwareSerial.h>
#include "config.h"

class GpsHandler {
public:
    GpsHandler();

    void begin();
    void update(); // Call in loop() to read NMEA stream

    bool     hasFix() const;
    bool     hasPps() const;
    uint8_t  getSatellites() const;
    uint16_t getHdop100() const; // HDOP * 100
    
    void getTimeStr(char* buffer, uint8_t len) const;
    void getDateStr(char* buffer, uint8_t len) const;
    void getGridLocator(char* buffer, uint8_t len) const; // Maidenhead QTH locator
    
    uint32_t getPpsCount() const;
    uint32_t getLastPpsMillis() const;

    // Called from 1PPS interrupt (Pin D2)
    void notifyPpsInterrupt();

private:
    void processNmeaSentence(char* sentence);
    void parseGga(char* sentence);
    void parseRmc(char* sentence);

    SoftwareSerial _serial;
    
    char     _nmea_buf[82];
    uint8_t  _nmea_pos;

    bool     _has_fix;
    uint8_t  _sats;
    uint16_t _hdop100;
    
    uint8_t  _hour;
    uint8_t  _minute;
    uint8_t  _second;
    bool     _time_valid;

    uint8_t  _day;
    uint8_t  _month;
    uint16_t _year;
    bool     _date_valid;

    char     _grid[7];
    bool     _grid_valid;

    volatile uint32_t _pps_count;
    volatile uint32_t _last_pps_millis;
    volatile bool     _pps_flag;
};

extern GpsHandler gpsHandler;

#endif // GPS_HANDLER_H
