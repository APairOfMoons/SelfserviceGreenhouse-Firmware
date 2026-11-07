//
// IHardwareData 的假实现（仅用于 PC 测试）。
//

#include "logic/FakeHardware.h"

namespace greenhouse {

FakeHardware::FakeHardware()
    : temperature(25.0F),
      air_humidity(50.0F),
      soil_humidity(50.0F),
      illuminance(5000U),
      has_enough_water(true),
      fail_temperature(false),
      fail_air_humidity(false),
      fail_soil_humidity(false),
      fail_illuminance(false),
      fail_water_level(false),
      fail_turntable(false),
      reject_actuators(false),
      reject_pump_off(false),
      fan_on(false),
      light_on(false),
      heater_on(false),
      pump_on(false),
      fan_toggles(0U),
      light_toggles(0U),
      heater_toggles(0U),
      pump_toggles(0U),
      turntable_angle(0),
      turntable_total_angle(0),
      turntable_calls(0U),
      turntable_attempts(0U)
{
}

bool FakeHardware::readTemperature(float *out_temperature)
{
    if (fail_temperature || (nullptr == out_temperature))
    {
        return false;
    }
    *out_temperature = temperature;
    return true;
}

bool FakeHardware::readAirHumidity(float *out_air_humidity)
{
    if (fail_air_humidity || (nullptr == out_air_humidity))
    {
        return false;
    }
    *out_air_humidity = air_humidity;
    return true;
}

bool FakeHardware::readSoilHumidity(float *out_soil_humidity)
{
    if (fail_soil_humidity || (nullptr == out_soil_humidity))
    {
        return false;
    }
    *out_soil_humidity = soil_humidity;
    return true;
}

bool FakeHardware::readIlluminance(uint16_t *out_illuminance)
{
    if (fail_illuminance || (nullptr == out_illuminance))
    {
        return false;
    }
    *out_illuminance = illuminance;
    return true;
}

bool FakeHardware::readWaterLevel(bool *out_has_enough_water)
{
    if (fail_water_level || (nullptr == out_has_enough_water))
    {
        return false;
    }
    *out_has_enough_water = has_enough_water;
    return true;
}

bool FakeHardware::setFanStatus(bool status)
{
    if (reject_actuators)
    {
        return false;
    }
    if (fan_on != status)
    {
        fan_toggles++;
        fan_on = status;
    }
    return true;
}

bool FakeHardware::setLightStatus(bool status)
{
    if (reject_actuators)
    {
        return false;
    }
    if (light_on != status)
    {
        light_toggles++;
        light_on = status;
    }
    return true;
}

bool FakeHardware::setHeaterStatus(bool status)
{
    if (reject_actuators)
    {
        return false;
    }
    if (heater_on != status)
    {
        heater_toggles++;
        heater_on = status;
    }
    return true;
}

bool FakeHardware::setPumpStatus(bool status)
{
    if (reject_actuators)
    {
        return false;
    }
    if (!status && reject_pump_off)
    {
        return false; // 模拟关泵被拒
    }
    if (pump_on != status)
    {
        pump_toggles++;
        pump_on = status;
    }
    return true;
}

bool FakeHardware::rotateTurntable(int deltaDegrees)
{
    turntable_attempts++;
    if (fail_turntable)
    {
        return false;
    }
    turntable_calls++;
    turntable_angle = static_cast<int16_t>(turntable_angle + deltaDegrees);
    turntable_total_angle = static_cast<int16_t>(turntable_total_angle + deltaDegrees);
    return true;
}

} // namespace greenhouse
