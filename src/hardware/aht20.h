//
// Created by Schwi on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_AHT20DEV_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_AHT20DEV_H
#include <cstdint>

typedef struct aht20 {
  const char *name;
  void (*init)();
  void (*read)(double *temperature, double *humidity);
  void (*reset)();
} AHT20Dev, *PAHT20Dev;

AHT20Dev *AHT20hDevGet();

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_AHT20DEV_H
