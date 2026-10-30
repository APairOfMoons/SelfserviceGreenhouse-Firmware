//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H


class HardwareDataManager
{
    public:

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
    bool setHeaterStatus(bool status);
    bool setPumpStatus(bool status);

    bool rotateTurntable(int deltaDegrees);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREDATAMANAGER_H
