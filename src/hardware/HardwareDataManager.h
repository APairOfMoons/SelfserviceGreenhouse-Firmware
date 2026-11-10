//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#include <cstdint>

extern volatile bool gI2CTxCplt;
extern volatile bool gI2CRxCplt;

void I2CWaitTxCplt();
void I2CWaitRxCplt();


namespace HardwareDataManager{

    //读取

    double readTemperature();
    double readAirHumidity();
    double readSoilHumidity();
    int readIlluminance();

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
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
