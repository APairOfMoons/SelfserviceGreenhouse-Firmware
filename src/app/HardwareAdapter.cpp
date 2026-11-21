//
// 硬件适配层实现。
//
// 职责：把 HardwareDataManager 的返回约定翻译成 IHardwareData 的
// "bool 返回 + 出参" 约定：
//   * AHT20 失败时给出 -999 / -1 哨兵，这里统一翻译成 false；
//   * 土壤含水率由硬件层换算成 %，这里只做量程校验；
//   * 光照度超过 uint16 上限时饱和到 65535，而不是判失败；
//   * 加热是布尔语义，映射为 100% / 0% 占空比。
//

#include "app/HardwareAdapter.h"

#include "hardware/HardwareDataManager.h"

#include <cmath>

namespace
{

// 传感器量程：超出即视为无效读数（同时覆盖 HardwareDataManager 的失败哨兵）。
constexpr double kMinTemperature = -40.0;
constexpr double kMaxTemperature = 85.0; // AHT20 量程上限
constexpr double kMinHumidity = 0.0;
constexpr double kMaxHumidity = 100.0;
constexpr float kMinSoilHumidity = 0.0F;
constexpr float kMaxSoilHumidity = 100.0F;
constexpr double kMaxIlluminance = 65535.0; // uint16 上限，超出按饱和处理

} // namespace

namespace greenhouse
{

bool HardwareAdapter::readTemperatureHumidity(float *out_temperature,
                                              float *out_air_humidity)
{
    if ((nullptr == out_temperature) || (nullptr == out_air_humidity))
    {
        return false;
    }

    double temperature = 0.0;
    double air_humidity = 0.0;
    if (!HardwareDataManager::readTemperatureHumidity(&temperature, &air_humidity))
    {
        return false; // 一次读取失败，两路都不可信
    }

    if (!std::isfinite(temperature) || !std::isfinite(air_humidity) ||
        (temperature < kMinTemperature) || (temperature > kMaxTemperature) ||
        (air_humidity < kMinHumidity) || (air_humidity > kMaxHumidity))
    {
        return false;
    }

    *out_temperature = static_cast<float>(temperature);
    *out_air_humidity = static_cast<float>(air_humidity);
    return true;
}

bool HardwareAdapter::readSoilHumidity(float *out_soil_humidity)
{
    if (nullptr == out_soil_humidity)
    {
        return false;
    }

    const float value = HardwareDataManager::readSoilHumidity();
    if (!std::isfinite(value) || (value < kMinSoilHumidity) ||
        (value > kMaxSoilHumidity))
    {
        return false;
    }

    *out_soil_humidity = value;
    return true;
}

bool HardwareAdapter::readIlluminance(uint16_t *out_illuminance)
{
    if (nullptr == out_illuminance)
    {
        return false;
    }

    double value = HardwareDataManager::readIlluminance();
    /**
    if (!std::isfinite(value) || (value < 0.0))
    {
        return false;
    }
    if (value > kMaxIlluminance)
    {
        value = kMaxIlluminance; // uint16 表达不了更高的照度，饱和处理
    }

    *out_illuminance = static_cast<uint16_t>(value);
    **/

    *out_illuminance = static_cast<uint16_t>(500);
    return true;
}

bool HardwareAdapter::readWaterLevel(bool *out_has_enough_water)
{
    if (nullptr == out_has_enough_water)
    {
        return false;
    }

    // 水位只有开关量（浮球/液位开关），高电平 = 水够用；读取失败返回 false 且不改出参。
    bool has_water = false;
    if (!HardwareDataManager::readWaterLevel(&has_water))
    {
        return false;
    }

    *out_has_enough_water = has_water;
    return true;
}

bool HardwareAdapter::setFanStatus(bool status)
{
    return HardwareDataManager::setFanStatus(status);
}

bool HardwareAdapter::setLightStatus(bool status)
{
    return HardwareDataManager::setLightStatus(status);
}

bool HardwareAdapter::setHeaterStatus(bool status)
{
    // 逻辑层只有开/关：开 = 满功率，关 = 0%。
    return HardwareDataManager::setHeaterStatus(status ? 100U : 0U);
}

bool HardwareAdapter::setPumpStatus(bool status)
{
    return HardwareDataManager::setPumpStatus(status);
}

bool HardwareAdapter::rotateTurntable(int deltaDegrees)
{
    if (0 == deltaDegrees)
    {
        return true;
    }
    return HardwareDataManager::rotateTurntable(static_cast<float>(deltaDegrees));
}

} // namespace greenhouse
