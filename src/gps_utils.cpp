#include <stdlib.h>
#include <vector>
#include <string>
#include <string.h>
#include <stdio.h>

#include "gps_utils.h"

const size_t log_buffer_length = 1024;
char log_buffer[log_buffer_length];

bool checksumPass(const std::string &str) {
    size_t pos = str.find("*");
    if (pos == std::string::npos) {
        snprintf(log_buffer, log_buffer_length,
            "NMEA sentence format error. character[*] not found. src=[%s]", str.c_str());
        printf("%s\n", log_buffer);
        return false;
    }

    unsigned int crc_derived = 0, crc_expected;
    for (unsigned int i = 1; i < pos; ++i) {
        crc_derived = crc_derived ^ str[i];
    }

    std::string crc_string = str.substr(pos + 1); /* do not include beginning # or $ */
    sscanf(crc_string.c_str(), "%x", &crc_expected);

    if (crc_derived != crc_expected) {
        snprintf(log_buffer, log_buffer_length,
            "GPRMC CRC error encountered. expected %x derived %x. src=[%s]", crc_expected, crc_derived, str.c_str());
        printf("%s\n", log_buffer);
        return false;
    }

    return true;
}

 /*
 * sample message from UBlox
 * $GNGGA,075956.00,3734.25906,N,12201.18133,W,2,12,0.83,16.6,M,-29.7,M,,0000*40`
 */
enum GprmcFields {
    GprmcMessage = 0,
    GprmcTime,
    GprmcStatus,
    GprmcLatitude,
    GprmcNs,
    GprmcLongitude,
    GprmcEw,
    GprmcKnots,
    GprmcCourse,
    GprmcDate,
    GprmcMagVar,
    GprmcMagEw,
    NumberOfGprmcFields
};

enum GnggaFields {
    GnggaMessage = 0,
    GnggaTime,
    GnggaLatitude,
    GnggaLatitudeHemisphere,
    GnggaLongitude,
    GnggaLongitudeHemisphere,
    GnggaFixQuality,
    GnggaNumberOfSatellites,
    GnggaHdop,
    GnggaAltitude,
    GnggaAltitudeUnits,
    GnggaGeoidHeight,
    GnggaGeoidHeightUnits,
    GnggaDgpsUpdateElapsedTime,
    GnggaDgpsStationIdNumber,
    NumberOfGnggaFields
};

enum GngstFields {
    GngstMessage = 0,
    GngstTime,
    GnRangeRms,
    GnStdMajor,
    GnStdMinor,
    GnOrient,
    GnGstStdDevLat,
    GnGstStdDevLon,
    GnStdAlt,
    NumberOfGngstFields
};

enum GngbsFields {
    GngbsMessage = 0,
    GngbsTime,
    GnGbsErrLat,
    GnGbsErrLon,
    GnErrAlt,
    GnSVId,
    GnProbMissedDetection,
    GnBias,
    GnBiasStdDev,
    GnSystemId,
    GnSignalId,
    NumberOfGngbsFields
};

/*
 * option == 0 - match any of the characters in the delimiter field
 * option == 1 - interpret the delimiter field as a string to be matched
 */
std::vector<std::string> splitFields(const std::string &input_string, const std::string &delimiters, int option = 0) {
    std::vector<std::string> fields;
    std::string empty_string;
    std::string str = input_string;
    size_t pos = (option == 0) ? str.find_first_of(delimiters) : str.find(delimiters);
    size_t add_length = (option == 0) ? 1 : delimiters.size();
    while (pos != std::string::npos) {
        if (pos > 0) {
            fields.push_back(str.substr(0, pos));
        } else if (pos == 0) {
            fields.push_back(empty_string);
        }
        str = str.substr(pos + add_length);
        pos = (option == 0) ? str.find_first_of(delimiters) : str.find(delimiters);
    }
    if (str.length()) {
        fields.push_back(str);
    } else {
        fields.push_back(empty_string);
    }
    return fields;
}

uint64_t UTCTimeFromGPRMCDateTimeStrings(const char *date_string, const char *time_string) {
    struct tm t;
    memset(&t, 0, sizeof(struct tm));
    char str[3];
    str[2] = 0;
    str[0] = date_string[0];
    str[1] = date_string[1];
    sscanf(str, "%02d", &t.tm_mday);
    str[0] = date_string[2];
    str[1] = date_string[3];
    sscanf(str, "%02d", &t.tm_mon);
    t.tm_mon--;
    str[0] = date_string[4];
    str[1] = date_string[5];
    sscanf(str, "%02d", &t.tm_year);
    t.tm_year += (2000 - 1900); /* 2017 comes as 17 */

    str[0] = time_string[0];
    str[1] = time_string[1];
    sscanf(str, "%02d", &t.tm_hour);
    t.tm_min = (time_string[2] - '0') * 10 + (time_string[3] - '0');
    t.tm_sec = (time_string[4] - '0') * 10 + (time_string[5] - '0');

    const uint32_t ms = (time_string[7] - '0') * 100 + (time_string[8] - '0') * 10;

    const uint64_t seconds = timegm(&t);
    return (seconds * 1000) + ms;
}

double convertNmeaToDegrees(const std::string &nmea_sentence) {
    int n_pos = nmea_sentence.find_first_of(".");
    if (n_pos == std::string::npos) { return 0.0; }
    std::string str1 = nmea_sentence.substr(0, n_pos);
    std::string str2 = nmea_sentence.substr(n_pos);
    int k = atoi(str1.c_str());
    double degrees = k / 100;
    int minutes = k % 100;
    double fraction = minutes + strtod(str2.c_str(), nullptr);
    degrees = degrees + fraction / 60.0;
    return degrees;
}

int parseGprmc(const std::string &str, uint64_t *timestamp, double *lat, double *lon) {

    const char *nmeaName = "RMC";
    const size_t expected_fields = NumberOfGprmcFields;

    bool passes_crc = checksumPass(str);
    if (passes_crc == false) {
        snprintf(log_buffer, log_buffer_length, "NMEA message fails CRC");
        printf("%s\n", log_buffer);
        return 0;
    }

    std::vector<std::string> fields = splitFields(str, ",");
    if ((fields.size() > 0) && (fields[0].find(nmeaName) == std::string::npos)) { return 1; }
    if (fields.size() < expected_fields) {
        snprintf(log_buffer, log_buffer_length,
            "GN%s sentence format error. %ld fields, expected %zd(min) fields\n", nmeaName, fields.size(), expected_fields);
        printf("%s\n", log_buffer);
        return 2;
    }

    std::string &time_str = fields[GprmcFields::GprmcTime];
    std::string &date_str = fields[GprmcFields::GprmcDate];
    uint64_t gps_time = UTCTimeFromGPRMCDateTimeStrings(date_str.c_str(), time_str.c_str());

    std::string &lat_str = fields[GprmcFields::GprmcLatitude];
    std::string &lon_str = fields[GprmcFields::GprmcLongitude];

    double gps_lat = convertNmeaToDegrees(lat_str.c_str());
    double gps_lon = convertNmeaToDegrees(lon_str.c_str());

    std::string &ns = fields[GprmcNs];
    std::string &ew = fields[GprmcEw];

    if (ns == "S") { gps_lat = -1.0 * gps_lat; }
    if (ew == "W") { gps_lon = -1.0 * gps_lon; }

    if (lat != 0) { *lat = gps_lat; }
    if (lon != 0) { *lon = gps_lon; }
    if (timestamp != 0) { *timestamp = gps_time; }

    return 0;
}

int parseGngga(const std::string &str) {

    const char *nmeaName = "GGA";
    const size_t expected_fields = NumberOfGnggaFields;

    bool passes_crc = checksumPass(str);
    if (passes_crc == false) {
        snprintf(log_buffer, log_buffer_length, "NMEA message fails CRC");
        printf("%s\n", log_buffer);
        return 0;
    }

    std::vector<std::string> fields = splitFields(str, ",");
    if ((fields.size() > 0) && (fields[0].find(nmeaName) == std::string::npos)) { return 1; }
    if (fields.size() < expected_fields) {
        snprintf(log_buffer, log_buffer_length,
            "GN%s sentence format error. %ld fields, expected %zd(min) fields\n", nmeaName, fields.size(), expected_fields);
        printf("%s\n", log_buffer);
        return 2;
    }

    return 0;
}

int parseGngbs(const std::string &str, double *lat_err, double *lon_err) {

    const char *nmeaName = "GBS";
    const size_t expected_fields = NumberOfGngbsFields;

    bool passes_crc = checksumPass(str);
    if (passes_crc == false) {
        snprintf(log_buffer, log_buffer_length, "NMEA message fails CRC");
        printf("%s\n", log_buffer);
        return 0;
    }

    std::vector<std::string> fields = splitFields(str, ",");
    if ((fields.size() > 0) && (fields[0].find(nmeaName) == std::string::npos)) { return 1; }
    if (fields.size() < expected_fields) {
        snprintf(log_buffer, log_buffer_length,
            "GN%s sentence format error. %ld fields, expected %zd(min) fields\n", nmeaName, fields.size(), expected_fields);
        printf("%s\n", log_buffer);
        return 2;
    }

    if (lat_err != 0) { *lat_err = stod(fields[GnGbsErrLat], nullptr); }
    if (lon_err != 0) { *lon_err = stod(fields[GnGbsErrLon], nullptr); }

    return 0;
}

int parseGngst(const std::string &str, double *lat_std_dev, double *lon_std_dev) {

    const char *nmeaName = "GST";
    const size_t expected_fields = NumberOfGngstFields;

    bool passes_crc = checksumPass(str);
    if (passes_crc == false) {
        snprintf(log_buffer, log_buffer_length, "NMEA message fails CRC");
        printf("%s\n", log_buffer);
        return 0;
    }

    std::vector<std::string> fields = splitFields(str, ",");
    if ((fields.size() > 0) && (fields[0].find(nmeaName) == std::string::npos)) { return 1; }
    if (fields.size() < expected_fields) {
        snprintf(log_buffer, log_buffer_length,
            "GNGST sentence format error. %ld fields, expected %zd(min) fields\n", fields.size(), expected_fields);
        printf("%s\n", log_buffer);
        return 2;
    }

    if (lat_std_dev != 0) { *lat_std_dev = stod(fields[GnGstStdDevLat], nullptr); }
    if (lon_std_dev != 0) { *lon_std_dev = stod(fields[GnGstStdDevLon], nullptr); }

    return 0;
}

