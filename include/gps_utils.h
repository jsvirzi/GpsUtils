#ifndef GPRMC_PARSER_H
#define GPRMC_PARSER_H

int parseGprmc(const std::string &str, uint64_t *timestamp, double *lat, double *lon);
int parseGngbs(const std::string &str, double *lat_err, double *lon_err);
int parseGngst(const std::string &str, double *lat_std_dev, double *lon_std_dev);

#endif
