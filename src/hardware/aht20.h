//
// Created by Schwi on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_AHT20DEV_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_AHT20DEV_H
#include <cstdint>

namespace aht20 {
uint8_t read_status();
void reset();
void measure();
void init();
void read(double *temperature, double *humidity);
} // namespace aht20

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_AHT20DEV_H
