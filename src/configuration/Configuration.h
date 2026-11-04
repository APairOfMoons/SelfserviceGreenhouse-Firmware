//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATION_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATION_H


struct GreenhouseConfiguration
{
    const int TURN_ANGLE_PER_DAY;

    // 温度(℃)
    const double MAX_TEMPERATURE;
    const double MIN_TEMPERATURE;

    // 空气湿度(%)
    const double MAX_AIR_HUMIDITY;
    const double MIN_AIR_HUMIDITY;

    // 土壤湿度(%)
    const double MAX_SOIL_HUMIDITY;
    const double MIN_SOIL_HUMIDITY;

    // 光照(lx)
    const double MAX_ILLUMINANCE;
    const double MIN_ILLUMINANCE;

    const int NETWORK_PORT;

};

constexpr GreenhouseConfiguration GREENHOUSE_CONFIGURATION = {
    10,

    30,
    10,
    90,
    30,
    55,
    30,
    100000,
    1200,

    25565
};

#endif //SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATION_H
