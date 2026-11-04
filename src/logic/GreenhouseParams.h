//
// 养护参数：只定义"有哪些参数、单位、合法范围"，具体数值由配置/硬件侧提供。
// 不带 const 成员，便于整体赋值（上位机改参、从存储加载）。
//
// 命名与 configuration/Configuration.h 的字段一一对应：
//   MAX_TEMPERATURE / MIN_TEMPERATURE        -> max_temperature / min_temperature
//   MAX_AIR_HUMIDITY / MIN_AIR_HUMIDITY      -> max_air_humidity / min_air_humidity
//   MAX_SOIL_HUMIDITY / MIN_SOIL_HUMIDITY    -> max_soil_humidity / min_soil_humidity
//   MAX_ILLUMINANCE / MIN_ILLUMINANCE        -> max_illuminance / min_illuminance
//   TURN_ANGLE_PER_DAY                       -> turn_angle_per_day
// 其余字段为本层新增的执行参数（浇水剂量、摆动节律等）。
//
// 注意：本文件不提供默认值。参数实例由调用方（配置模块 / 硬件侧 / 存储）填充后
// 传给 LogicController；传参前后都可调用 normalizeParams() 做一次钳位。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_GREENHOUSEPARAMS_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_GREENHOUSEPARAMS_H

#include <stdint.h>

namespace greenhouse {

struct GreenhouseParams
{
    // ---------- 温度(℃) ----------
    float min_temperature;        // 低于此值开加热
    float max_temperature;        // 高于此值切通风并停加热
    float temperature_hysteresis; // 回滞带宽

    // ---------- 空气湿度(%) ----------
    float min_air_humidity;
    float max_air_humidity;        // 高于此值开风扇排湿
    float air_humidity_hysteresis;

    // ---------- 光照 ----------
    uint16_t min_illuminance;        // 低于此值需要补光
    uint32_t illuminance_min_on_ms;  // 补光最短点亮时间
    uint32_t day_start_ms;           // 白天开始（从 00:00 起的毫秒偏移）
    uint32_t day_length_ms;          // 白天持续时长

    // ---------- 土壤湿度(%) ----------
    float min_soil_humidity;         // 低于此值开始浇水
    float max_soil_humidity;         // 达到此值判定已浇透
    uint32_t pump_dose_ms;           // 单次浇水时长
    uint32_t pump_min_interval_ms;   // 两次浇水的最小间隔
    uint32_t pump_max_per_day;       // 每日浇水次数上限

    // ---------- 转盘 ----------
    int16_t max_turn_angle;          // 左右摆动幅度（±）
    int16_t turn_step_angle;         // 单次摆动步进角度
    uint32_t turn_interval_ms;       // 摆动周期
    int16_t turn_angle_per_day;      // 对应 TURN_ANGLE_PER_DAY

    // ---------- 调度与容错 ----------
    uint32_t sample_period_ms;
    uint8_t sensor_fail_limit;
    uint32_t manual_timeout_ms;

    // ---------- 时间 ----------
    bool has_rtc;
};

// 修正越界/自相矛盾的参数；返回 false 表示做了修正。
// 不填默认值：零值/未赋值的字段会被钳到合法下界，调用方应自行提供有效参数。
bool normalizeParams(GreenhouseParams *params);

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_GREENHOUSEPARAMS_H
