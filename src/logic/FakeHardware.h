//
// IHardwareData 的假实现：用于在 PC 上跑逻辑测试，可脚本化输入与故障注入。
// 字段命名与 LogicData 对齐（temperature / air_humidity / soil_humidity /
// illuminance / has_enough_water / fan_on / light_on / heater_on）。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_FAKEHARDWARE_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_FAKEHARDWARE_H

#include "logic/IHardwareData.h"

namespace greenhouse {

class FakeHardware : public IHardwareData
{
    public:
    FakeHardware();

    // ---------- 可脚本化的输入 ----------
    float temperature;
    float air_humidity;
    float soil_humidity;
    uint16_t illuminance;
    bool has_enough_water;

    // ---------- 故障注入 ----------
    bool fail_temperature;
    bool fail_air_humidity;
    bool fail_soil_humidity;
    bool fail_illuminance;
    bool fail_water_level;
    bool fail_turntable;
    bool reject_actuators;
    bool reject_pump_off;   // 模拟"关泵被拒"（验证重试逻辑）

    // ---------- 观测到的输出 ----------
    bool fan_on;
    bool light_on;
    bool heater_on;
    bool pump_on;

    uint32_t fan_toggles;
    uint32_t light_toggles;
    uint32_t heater_toggles;
    uint32_t pump_toggles;

    int16_t turntable_angle;
    int16_t turntable_total_angle;
    uint32_t turntable_calls;
    uint32_t turntable_attempts; // 含失败在内的尝试次数

    // ---------- IHardwareData ----------
    bool readTemperature(float *out_temperature) override;
    bool readAirHumidity(float *out_air_humidity) override;
    bool readSoilHumidity(float *out_soil_humidity) override;
    bool readIlluminance(uint16_t *out_illuminance) override;
    bool readWaterLevel(bool *out_has_enough_water) override;

    bool setFanStatus(bool status) override;
    bool setLightStatus(bool status) override;
    bool setHeaterStatus(bool status) override;
    bool setPumpStatus(bool status) override;

    bool rotateTurntable(int deltaDegrees) override;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_FAKEHARDWARE_H
