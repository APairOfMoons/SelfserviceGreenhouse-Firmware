//
// Created by APairOfMoons on 2026/10/30.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_DATASTRUCTURE_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_DATASTRUCTURE_H


struct LogicData
{
    double temperature;
    double air_humidity;
    double soil_humidity;
    int illuminance;

    bool has_enough_water;

    bool fan_on;
    bool light_on;
    bool heater_on;
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_DATASTRUCTURE_H
