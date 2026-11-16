//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#include <cstdint>

void I2C0WaitTxCplt();
void I2C0WaitRxCplt();

void I2C1WaitTxCplt();
void I2C1WaitRxCplt();

void ADC0WaitScanCplt();

namespace HardwareDataManager {
void peripheral_init();
// 读取

// 温湿度同源（AHT20）：一次测量同时返回两个值，避免重复触发 75 ms 测量。
// 返回 false 表示本次读数无效（温度或湿度任一非法）。
bool readTemperatureHumidity(double *temperature, double *air_humidity);
// 土壤含水率，单位 %（0~100），内部完成 ADC 原始值到百分比的换算。
float readSoilHumidity();
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
