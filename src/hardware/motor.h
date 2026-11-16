//
// Created by Schwi on 2026/11/9.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_MOTOR_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_MOTOR_H
#include <cstdint>

typedef enum motor_direction {
  MOTOR_DIRECTION_CW,
  MOTOR_DIRECTION_CCW
} motor_direction;

typedef struct MotorDev {
  const char *name;
  void (*rotate)(float angle, uint8_t direction, uint16_t delay_ms);
  void (*stop)();
} MotorDev, *PMotorDev;

MotorDev *MotorDevGet();

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_MOTOR_H
