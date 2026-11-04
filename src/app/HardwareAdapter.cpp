//
// 硬件适配层实现。
//

#include "app/HardwareAdapter.h"

#include "hardware/HardwareDataManager.h"

namespace
{

// 传感器量程：超出即视为无效读数（同时覆盖 HardwareDataManager 的失败哨兵）。
constexpr double kMinTemperature = -40.0;
constexpr double kMaxTemperature = 125.0;
constexpr double kMinHumidity = 0.0;
constexpr double kMaxHumidity = 100.0;
constexpr int kMinIlluminance = 0;
constexpr int kMaxIlluminance = 65535;

} // namespace

namespace greenhouse
{

bool HardwareAdapter::readTemperature(float *out_temperature)
{
    if (nullptr == out_temperature)
    {
        return false;
    }

    const double value = HardwareDataManager::readTemperature();
    if ((value < kMinTemperature) || (value > kMaxTemperature))
    {
        return false; // 包含 65535 这个失败哨兵
    }

    *out_temperature = static_cast<float>(value);
    return true;
}

bool HardwareAdapter::readAirHumidity(float *out_air_humidity)
{
    if (nullptr == out_air_humidity)
    {
        return false;
    }

    const double value = HardwareDataManager::readAirHumidity();
    if ((value < kMinHumidity) || (value > kMaxHumidity))
    {
        return false;
    }

    *out_air_humidity = static_cast<float>(value);
    return true;
}

bool HardwareAdapter::readSoilHumidity(float *out_soil_humidity)
{
    if (nullptr == out_soil_humidity)
    {
        return false;
    }

    const double value = HardwareDataManager::readSoilHumidity();
    if ((value < kMinHumidity) || (value > kMaxHumidity))
    {
        return false;
    }

    *out_soil_humidity = static_cast<float>(value);
    return true;
}

bool HardwareAdapter::readIlluminance(uint16_t *out_illuminance)
{
    if (nullptr == out_illuminance)
    {
        return false;
    }

    const int value = HardwareDataManager::readIlluminance();
    if ((value < kMinIlluminance) || (value > kMaxIlluminance))
    {
        return false;
    }

    *out_illuminance = static_cast<uint16_t>(value);
    return true;
}

bool HardwareAdapter::readWaterLevel(bool *out_has_enough_water)
{
    if (nullptr == out_has_enough_water)
    {
        return false;
    }

    // 水位只有开关量：readWaterLevel() 的返回值直接表示"水是否足够"。
    // 注意：HardwareDataManager 目前尚未实现该函数，落地前会返回不确定值；
    // 逻辑层对"读失败/缺水"统一按缺水处理（停泵保护），因此是安全侧退化。
    *out_has_enough_water = HardwareDataManager::readWaterLevel();
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
    return HardwareDataManager::setHeaterStatus(status);
}

bool HardwareAdapter::setPumpStatus(bool status)
{
    return HardwareDataManager::setPumpStatus(status);
}

bool HardwareAdapter::rotateTurntable(int deltaDegrees)
{
    return HardwareDataManager::rotateTurntable(deltaDegrees);
}

} // namespace greenhouse