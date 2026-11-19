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
// 返回 false 表示本次读数无效（通信失败、超时或温度/湿度任一超量程）。
bool readTemperatureHumidity(double *temperature, double *air_humidity);
// 土壤含水率，单位 %（0~100），内部完成 ADC 原始值到百分比的换算。
// ADC 未就绪、扫描失败/超时或读取失败时返回 < 0 的无效哨兵值（-1.0F）。
float readSoilHumidity();
// 光照度，单位 lx（>= 0）。I2C 通信失败/超时/中止时返回 < 0 的无效哨兵值（-1.0）。
double readIlluminance();

bool readFanStatus();
bool readLightStatus();
bool readHeaterStatus();
bool readPumpStatus();

// 水位开关量：成功返回 true 并写入 *has_water（true = 水够用）；读取失败返回 false 且不改出参。
bool readWaterLevel(bool *has_water);
// 兼容无参接口：读取失败时按缺水（false）保守处理。
bool readWaterLevel();

// 操作

bool setFanStatus(bool status);
bool setLightStatus(bool status);
// duty_cycle 取值 0~100；0 时设为 0% 占空比并停止 GPT 输出（readHeaterStatus 返回 false）。
bool setHeaterStatus(const uint8_t duty_cycle);
bool setPumpStatus(bool status);

// deltaDegrees == 0 时不触发步进动作并直接返回 true。
bool rotateTurntable(float deltaDegrees);
}; // namespace HardwareDataManager

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
