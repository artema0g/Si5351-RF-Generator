#include "gps_handler.h"

GpsHandler gpsHandler;

static char* get_field(char* str, uint8_t field_num) {
    while (field_num > 0) {
        str = strchr(str, ',');
        if (!str) return NULL;
        str++;
        field_num--;
    }
    return str;
}

static uint8_t parse2digits(const char* p) {
    if (!p || p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
    return (uint8_t)((p[0] - '0') * 10 + (p[1] - '0'));
}

GpsHandler::GpsHandler()
    : _serial(PIN_GPS_RX, PIN_GPS_TX),
      _nmea_pos(0),
      _has_fix(false),
      _sats(0),
      _hdop100(9999),
      _hour(0), _minute(0), _second(0), _time_valid(false),
      _day(0), _month(0), _year(0), _date_valid(false),
      _grid_valid(false),
      _pps_count(0),
      _last_pps_millis(0),
      _pps_flag(false)
{
    strcpy(_grid, "------");
}

void GpsHandler::begin() {
    _serial.begin(GPS_BAUD_RATE);
}

void GpsHandler::notifyPpsInterrupt() {
    _pps_count++;
    _last_pps_millis = millis();
    _pps_flag = true;
}

void GpsHandler::update() {
    while (_serial.available() > 0) {
        char c = (char)_serial.read();
        if (c == '$') {
            _nmea_pos = 0;
            _nmea_buf[0] = '$';
            _nmea_pos = 1;
        } else if (_nmea_pos > 0) {
            if (c == '\r' || c == '\n') {
                _nmea_buf[_nmea_pos] = 0;
                processNmeaSentence(_nmea_buf);
                _nmea_pos = 0;
            } else if (_nmea_pos < sizeof(_nmea_buf) - 1) {
                _nmea_buf[_nmea_pos++] = c;
            } else {
                _nmea_pos = 0; // Buffer overflow safeguard
            }
        }
    }
}

void GpsHandler::processNmeaSentence(char* sentence) {
    // Check sentence talker/formatter: $..GGA or $..RMC (u-blox sends $GPGGA/$GNGGA, $GPRMC/$GNRMC)
    if (strncmp(sentence + 3, "GGA", 3) == 0) {
        parseGga(sentence);
    } else if (strncmp(sentence + 3, "RMC", 3) == 0) {
        parseRmc(sentence);
    }
}

void GpsHandler::parseGga(char* sentence) {
    // Field 1: UTC Time
    char* fTime = get_field(sentence, 1);
    if (fTime && strlen(fTime) >= 6) {
        _hour = parse2digits(fTime);
        _minute = parse2digits(fTime + 2);
        _second = parse2digits(fTime + 4);
        _time_valid = true;
    }

    // Field 6: Fix Quality (0 = invalid, >0 = GPS/DGPS fix)
    char* fFix = get_field(sentence, 6);
    if (fFix) {
        _has_fix = (*fFix > '0');
    }

    // Field 7: Number of satellites in view
    char* fSats = get_field(sentence, 7);
    if (fSats) {
        _sats = (uint8_t)atoi(fSats);
    }

    // Field 8: HDOP (e.g. "0.9" or "1.5")
    char* fHdop = get_field(sentence, 8);
    if (fHdop && *fHdop != ',') {
        uint16_t val = (uint16_t)(atoi(fHdop) * 100);
        char* dot = strchr(fHdop, '.');
        if (dot && *(dot + 1) >= '0' && *(dot + 1) <= '9') {
            val += (uint16_t)((*(dot + 1) - '0') * 10);
            if (*(dot + 2) >= '0' && *(dot + 2) <= '9') {
                val += (uint16_t)(*(dot + 2) - '0');
            }
        }
        _hdop100 = val;
    }
}

void GpsHandler::parseRmc(char* sentence) {
    // Field 2: Status ('A' = Active / Fix, 'V' = Void)
    char* fStat = get_field(sentence, 2);
    if (fStat) {
        if (*fStat == 'A') {
            _has_fix = true;
        } else if (*fStat == 'V') {
            _has_fix = false;
        }
    }

    // Field 1: UTC Time
    char* fTime = get_field(sentence, 1);
    if (fTime && strlen(fTime) >= 6) {
        _hour = parse2digits(fTime);
        _minute = parse2digits(fTime + 2);
        _second = parse2digits(fTime + 4);
        _time_valid = true;
    }

    // Fields 3-6: Coordinates for Maidenhead Grid Locator calculation
    char* fLat = get_field(sentence, 3);
    char* fNs  = get_field(sentence, 4);
    char* fLon = get_field(sentence, 5);
    char* fEw  = get_field(sentence, 6);

    if (fLat && fNs && fLon && fEw && strlen(fLat) >= 4 && strlen(fLon) >= 5) {
        // Latitude ddmm.mmmm
        long lat_deg = parse2digits(fLat);
        long lat_min = parse2digits(fLat + 2);
        long lat_mdeg = lat_deg * 1000L + (lat_min * 1000L) / 60L;
        if (*fNs == 'S') lat_mdeg = -lat_mdeg;
        lat_mdeg += 90000L; // normalize to 0..180000

        // Longitude dddmm.mmmm
        long lon_deg = (fLon[0] - '0') * 100L + parse2digits(fLon + 1);
        long lon_min = parse2digits(fLon + 3);
        long lon_mdeg = lon_deg * 1000L + (lon_min * 1000L) / 60L;
        if (*fEw == 'W') lon_mdeg = -lon_mdeg;
        lon_mdeg += 180000L; // normalize to 0..360000

        if (lon_mdeg >= 0 && lon_mdeg < 360000L && lat_mdeg >= 0 && lat_mdeg < 180000L) {
            _grid[0] = 'A' + (char)(lon_mdeg / 20000L);
            _grid[1] = 'A' + (char)(lat_mdeg / 10000L);
            lon_mdeg %= 20000L;
            lat_mdeg %= 10000L;

            _grid[2] = '0' + (char)(lon_mdeg / 2000L);
            _grid[3] = '0' + (char)(lat_mdeg / 1000L);
            lon_mdeg %= 2000L;
            lat_mdeg %= 1000L;

            _grid[4] = 'a' + (char)(lon_mdeg / 83L);
            _grid[5] = 'a' + (char)(lat_mdeg / 41L);
            _grid[6] = 0;
            _grid_valid = true;
        }
    }

    // Field 9: Date DDMMYY
    char* fDate = get_field(sentence, 9);
    if (fDate && strlen(fDate) >= 6) {
        _day = parse2digits(fDate);
        _month = parse2digits(fDate + 2);
        _year = 2000 + parse2digits(fDate + 4);
        _date_valid = true;
    }
}

bool GpsHandler::hasFix() const {
    return (_has_fix && _sats >= 4);
}

bool GpsHandler::hasPps() const {
    return (_last_pps_millis > 0 && (millis() - _last_pps_millis < 1500));
}

uint8_t GpsHandler::getSatellites() const {
    return _sats;
}

uint16_t GpsHandler::getHdop100() const {
    return _hdop100;
}

void GpsHandler::getTimeStr(char* buffer, uint8_t len) const {
    if (len < 9) return;
    if (_time_valid) {
        buffer[0] = '0' + (_hour / 10);
        buffer[1] = '0' + (_hour % 10);
        buffer[2] = ':';
        buffer[3] = '0' + (_minute / 10);
        buffer[4] = '0' + (_minute % 10);
        buffer[5] = ':';
        buffer[6] = '0' + (_second / 10);
        buffer[7] = '0' + (_second % 10);
        buffer[8] = 0;
    } else {
        strcpy(buffer, "--:--:--");
    }
}

void GpsHandler::getDateStr(char* buffer, uint8_t len) const {
    if (len < 11) return;
    if (_date_valid) {
        buffer[0] = '0' + (_day / 10);
        buffer[1] = '0' + (_day % 10);
        buffer[2] = '.';
        buffer[3] = '0' + (_month / 10);
        buffer[4] = '0' + (_month % 10);
        buffer[5] = '.';
        buffer[6] = '0' + ((_year / 1000) % 10);
        buffer[7] = '0' + ((_year / 100) % 10);
        buffer[8] = '0' + ((_year / 10) % 10);
        buffer[9] = '0' + (_year % 10);
        buffer[10] = 0;
    } else {
        strcpy(buffer, "--.--.----");
    }
}

void GpsHandler::getGridLocator(char* buffer, uint8_t len) const {
    if (len < 7) return;
    if (_grid_valid) {
        strncpy(buffer, _grid, len);
    } else {
        strcpy(buffer, "------");
    }
}

uint32_t GpsHandler::getPpsCount() const {
    return _pps_count;
}

uint32_t GpsHandler::getLastPpsMillis() const {
    return _last_pps_millis;
}
