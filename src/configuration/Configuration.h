//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATION_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATION_H


struct GreenhouseConfiguration
{
    const int TURN_ANGLE_PER_DAY;

    const double MAX_TEMPERATURE;
    const double MIN_TEMPERATURE;
    const double MAX_AIR_HUMIDITY;
    const double MIN_AIR_HUMIDITY;

    const int NETWORK_PORT;

};

constexpr GreenhouseConfiguration GREENHOUSE_CONFIGURATION = {
    10,

    30,
    10,
    90,
    30,

    25565
};

#endif //SELFSERVICEGREENHOUSE_FIRMWARE_CONFIGURATION_H
