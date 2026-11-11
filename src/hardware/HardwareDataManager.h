//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#include <cstdint>

extern volatile bool gI2C0TxCplt;
extern volatile bool gI2C0RxCplt;

extern volatile bool gI2C1TxCplt;
extern volatile bool gI2C1RxCplt;

void I2C0WaitTxCplt();
void I2C0WaitRxCplt();

void I2C1WaitTxCplt();
void I2C1WaitRxCplt();

namespace HardwareDataManager {
void peripheral_init();
// 读取

double readTemperature();
double readAirHumidity();
double readSoilHumidity();
double readIlluminance();

bool readFanStatus();
bool readLightStatus();
bool readHeaterStatus();
bool readPumpStatus();

bool readWaterLevel();

// 操作

bool setFanStatus(bool status);
bool setLightStatus(bool status);
bool setHeaterStatus(const uint8_t duty_cycle);
bool setPumpStatus(bool status);

bool rotateTurntable(float deltaDegrees);
}; // namespace HardwareDataManager

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
