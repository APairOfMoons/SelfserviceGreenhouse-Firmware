//
// 参数合法性检查。
// 注意：本文件不定义任何养护数值；默认值/预设由配置或硬件侧提供。
//

#include "logic/GreenhouseParams.h"

namespace greenhouse {
namespace {

constexpr uint32_t kMinute = 60UL * 1000UL;
constexpr uint32_t kHour = 60UL * kMinute;

bool clampFloat(float *value, float low, float high)
{
    if (*value < low)
    {
        *value = low;
        return false;
    }
    if (*value > high)
    {
        *value = high;
        return false;
    }
    return true;
}

} // namespace

bool normalizeParams(GreenhouseParams *params)
{
    if (nullptr == params)
    {
        return false;
    }

    bool ok = true;

    // 温度：-10..50 ℃，且保持 min <= max - 1 ℃
    ok = clampFloat(&params->min_temperature, -10.0F, 49.0F) && ok;
    ok = clampFloat(&params->max_temperature, params->min_temperature + 1.0F, 50.0F) && ok;
    ok = clampFloat(&params->temperature_hysteresis, 0.1F, 10.0F) && ok;

    // 空气湿度：0..100 %
    ok = clampFloat(&params->min_air_humidity, 0.0F, 99.0F) && ok;
    ok = clampFloat(&params->max_air_humidity, params->min_air_humidity + 1.0F, 100.0F) && ok;
    ok = clampFloat(&params->air_humidity_hysteresis, 0.5F, 20.0F) && ok;

    // 土壤湿度
    ok = clampFloat(&params->min_soil_humidity, 1.0F, 99.0F) && ok;
    ok = clampFloat(&params->max_soil_humidity, params->min_soil_humidity, 100.0F) && ok;

    // 光照：min_illuminance 为 uint16_t，上限取传感量程内的 60000 lx
    if (params->min_illuminance > 60000U)
    {
        params->min_illuminance = 60000U;
        ok = false;
    }

    // 时间窗口
    if (params->day_length_ms < kMinute)
    {
        params->day_length_ms = kMinute;
        ok = false;
    }
    if (params->day_length_ms > (24UL * kHour))
    {
        params->day_length_ms = 24UL * kHour;
        ok = false;
    }
    if (params->day_start_ms >= (24UL * kHour))
    {
        params->day_start_ms %= (24UL * kHour);
        ok = false;
    }

    // 浇水
    if (0UL == params->pump_dose_ms)
    {
        params->pump_dose_ms = 200UL;
        ok = false;
    }
    if (params->pump_dose_ms > (10UL * kMinute))
    {
        params->pump_dose_ms = 10UL * kMinute;
        ok = false;
    }
    if (params->pump_min_interval_ms < params->pump_dose_ms)
    {
        params->pump_min_interval_ms = params->pump_dose_ms + kMinute;
        ok = false;
    }
    if (0U == params->pump_max_per_day)
    {
        params->pump_max_per_day = 1U;
        ok = false;
    }
    if (params->pump_max_per_day > 12U)
    {
        params->pump_max_per_day = 12U;
        ok = false;
    }

    // 转盘
    if (params->max_turn_angle < 0)
    {
        params->max_turn_angle = static_cast<int16_t>(-params->max_turn_angle);
        ok = false;
    }
    if (params->max_turn_angle > 180)
    {
        params->max_turn_angle = 180;
        ok = false;
    }
    if (params->turn_step_angle <= 0)
    {
        params->turn_step_angle = 15;
        ok = false;
    }
    if (params->turn_step_angle > params->max_turn_angle)
    {
        params->turn_step_angle = params->max_turn_angle;
        ok = false;
    }
    if (params->turn_interval_ms < 1000UL)
    {
        params->turn_interval_ms = 1000UL;
        ok = false;
    }

    // 调度
    if (params->sample_period_ms < 10UL)
    {
        params->sample_period_ms = 10UL;
        ok = false;
    }
    if (params->sample_period_ms > (60UL * kMinute))
    {
        params->sample_period_ms = 60UL * kMinute;
        ok = false;
    }
    if (0U == params->sensor_fail_limit)
    {
        params->sensor_fail_limit = 1U;
        ok = false;
    }
    if (params->manual_timeout_ms < kMinute)
    {
        params->manual_timeout_ms = kMinute;
        ok = false;
    }

    return ok;
}

} // namespace greenhouse
