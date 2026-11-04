//
// ControllerSnapshot -> MonitoringData 转换实现。
//

#include "logic/MonitoringDataMapper.h"

namespace greenhouse {

bool toMonitoringData(const ControllerSnapshot &snapshot, MonitoringData *out_data)
{
    if (nullptr == out_data)
    {
        return false;
    }

    out_data->temperature = static_cast<double>(snapshot.temperature);
    out_data->air_humidity = static_cast<double>(snapshot.air_humidity);
    out_data->soil_humidity = static_cast<double>(snapshot.soil_humidity);

    // 传感器量程 0..65535 lx，int 足够容纳，不做截断。
    out_data->illuminance = static_cast<int>(snapshot.illuminance);

    out_data->has_enough_water = snapshot.has_enough_water;

    out_data->fan_on = snapshot.fan_on;
    out_data->light_on = snapshot.light_on;
    out_data->heater_on = snapshot.heater_on;

    return true;
}

} // namespace greenhouse
