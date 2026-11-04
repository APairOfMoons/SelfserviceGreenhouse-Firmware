//
// 温室控制逻辑：输入一次采样快照，输出一路执行器命令。
//
// 命名约定（与既有模块对齐）：
//   * 读数/状态字段沿用 communication/data/DataStructure.h：
//     temperature / air_humidity / soil_humidity / illuminance / has_enough_water /
//     fan_on / light_on / heater_on；
//   * 阈值字段沿用 configuration/Configuration.h 的词汇（min_/max_ + 变量名）；
//   * 硬件动作沿用 hardware/HardwareDataManager.h 的动词（readXxx / setXxxStatus）；
//   * 故障位与 communication/exception/ExceptionHandler.h 的枚举同源，
//     映射见 ExceptionMapping.h。
//
// 设计约束：无 FSP / FreeRTOS 依赖、无动态内存、无异常、时间由 step() 传入。
//
// 关于执行器阻塞（与 IHardwareData 的约定一致）：
//   setXxxStatus() 是"返回 true = 动作已确认完成"的同步接口，因此一次 step() 的实际
//   耗时 = 决策 + 各执行器阻塞时间之和。调用方需保证单个执行器阻塞 <= 20 ms，
//   否则采样节拍会被拉长；长动作（转盘、水泵）不应靠阻塞主循环完成。
//   计时以 step() 传入的 now_ms 为准，阻塞期间时间不推进，因此浇水剂量最多
//   超出"本轮阻塞总时长"（继电器动作量级为毫秒，可忽略）。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_LOGICCONTROLLER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_LOGICCONTROLLER_H

#include "logic/GreenhouseParams.h"
#include "logic/IHardwareData.h"

namespace greenhouse {

enum class OperatingMode : uint8_t
{
    AUTO = 0,
    MANUAL = 1,
    OFF = 2,
};

enum class OutputId : uint8_t
{
    FAN = 0,
    LIGHT = 1,
    HEATER = 2,
    PUMP = 3,
};

// 冲突仲裁策略：多个环境量同时越界时，按这里的规则决定谁让路。
// 成员都带保守默认值，硬件/配置侧可以整体替换（setConflictPolicy）。
struct ConflictPolicy
{
    // 超温时关补光：补光灯本身是热源，越热越不能开。
    bool suppress_light_on_over_temperature = true;

    // 加热时不常开风扇：排风会把刚加热的空气抽走，热量白烧。
    bool block_fan_while_heating = true;

    // 湿度高到这个程度（max_air_humidity + 该值）才允许破例通风。
    float fan_humidity_override_pct = 10.0F;

    // 破例通风采用脉冲：每 fan_pulse_interval_ms 开 fan_pulse_on_ms。
    uint32_t fan_pulse_on_ms = 120000UL;      // 2 min
    uint32_t fan_pulse_interval_ms = 1800000UL; // 30 min

    // 超温时暂停浇水：高温浇水易烫根，且蒸发快、水量算不准。
    bool pump_blocked_on_over_temperature = true;

    // 无时间基准（无 RTC / 未校时）时，补光是否退化为"只看光照阈值"。
    // true = 昼夜判断失效也让补光工作；false = 宁可不补光。
    bool allow_light_without_time = true;

    // 加热与浇水互斥（同一路电源/避免冷热冲击）。
    bool heater_pump_mutex = true;

    // 同时动作的执行器上限（按 加热 > 泵 > 风扇 > 补光 的优先级保留）。
    // 默认 4 = 不限；电源/继电器有电流约束时由硬件侧改成 2 或 3。
    uint8_t max_simultaneous_outputs = 4U;
};

// 故障位，可同时置位多个。带 _EXCEPTION 对应项的见 ExceptionMapping.h。
enum FaultBits : uint32_t
{
    FAULT_NONE = 0U,
    FAULT_WATER_LACK = 1U << 0,        // 水箱缺水 -> WATER_LACK_EXCEPTION
    FAULT_TEMPERATURE_SENSOR = 1U << 1,
    FAULT_AIR_HUMIDITY_SENSOR = 1U << 2,
    FAULT_SOIL_HUMIDITY_SENSOR = 1U << 3,
    FAULT_ILLUMINANCE_SENSOR = 1U << 4,
    FAULT_OVER_TEMPERATURE = 1U << 5,
    FAULT_PUMP_TIMEOUT = 1U << 6,
    FAULT_TURNTABLE_FAILED = 1U << 7,  // -> TURNTABLE_FAILED_EXCEPTION
    FAULT_TIME_UNSET = 1U << 8,
    FAULT_AIR_HUMIDITY_LOW = 1U << 9,  // 湿度过低且无加湿手段，只做诊断上报
};

// 对外只读的状态快照，字段与 LogicData 一一对应，可直接用于上报。
struct ControllerSnapshot
{
    float temperature;
    float air_humidity;
    float soil_humidity;
    uint16_t illuminance;
    bool has_enough_water;

    bool fan_on;
    bool light_on;
    bool heater_on;
    bool pump_on;

    int16_t turn_angle;
    bool turntable_moving;

    uint32_t faults;
    OperatingMode mode;
    bool time_valid;
    uint32_t uptime_ms;
    uint32_t pump_runs_today;
};

class LogicController
{
    public:
    LogicController(IHardwareData &hw, const GreenhouseParams &params);

    // ---------- 配置 ----------
    void setParams(const GreenhouseParams &params);
    const GreenhouseParams &params() const;

    // 冲突仲裁策略（超温禁补光、低温高湿时通风让路等）。
    void setConflictPolicy(const ConflictPolicy &policy);
    const ConflictPolicy &conflictPolicy() const;

    void setMode(OperatingMode mode, uint32_t now_ms);
    OperatingMode mode() const;

    // 手动控制：仅 MANUAL 模式下生效；超时后自动回到 AUTO。
    // 泵在缺水时始终拒绝启动，返回 false。
    bool setManualOutput(OutputId id, bool on, uint32_t now_ms);

    // ---------- 时间 ----------
    // 由上层（RTC 或联网校时）提供当天已过的毫秒数；未校时前不要调用。
    void setTimeOfDay(uint32_t ms_of_day);
    void clearTimeOfDay();

    // ---------- 主循环 ----------
    // 需周期性调用（建议 100 ms ~ 1 s）。内部按 sample_period_ms 控制采样频率。
    void step(uint32_t now_ms);

    const ControllerSnapshot &snapshot() const;
    uint32_t faults() const;

    static constexpr uint32_t kMinSwitchMs = 30UL * 1000UL; // 加热/风扇最短切换间隔

    private:
    struct Channel
    {
        float value;        // 最近一次有效值
        uint8_t fail_count; // 连续失败次数
        bool valid;         // 是否拿到过有效值
    };

    bool sampleDue(uint32_t now_ms) const;
    void sample(uint32_t now_ms);
    void evaluate(uint32_t now_ms);
    void updateTemperatureControl(uint32_t now_ms);
    void updateAirHumidityControl(uint32_t now_ms);
    void updateIlluminance(uint32_t now_ms);
    void updateIrrigation(uint32_t now_ms);
    void updateTurntable(uint32_t now_ms);
    void applySafeState(uint32_t now_ms);
    void applyConflictPolicy(uint32_t now_ms);

    bool fanPulseOpen(uint32_t now_ms) const;
    uint8_t countOutputsOn() const;

    bool isDaytime(uint32_t now_ms) const;
    uint32_t timeOfDay(uint32_t now_ms) const;
    uint32_t dayIndex(uint32_t now_ms) const;
    void setOutput(OutputId id, bool on);
    void retryOutputs();
    static uint8_t indexOf(OutputId id);
    void reportFault(uint32_t bits, bool active);

    bool sensorOk(const Channel &channel) const;
    bool waterOk() const;
    bool pumpAllowed(uint32_t now_ms);
    void startPump(uint32_t now_ms);
    void stopPump();

    static constexpr uint32_t kOutputRetryMs = 1000UL; // 执行器命令重试间隔
    static constexpr uint8_t kTurnFailLimit = 5U;      // 转盘连续失败上限
    static constexpr uint32_t kTurnRetryMs = 60000UL;  // 转盘失败后的重试间隔

    IHardwareData &hw_;
    GreenhouseParams params_;
    ConflictPolicy policy_;
    ControllerSnapshot snap_;
    uint32_t fault_bits_;

    OperatingMode mode_;
    uint32_t manual_deadline_ms_;
    bool manual_fan_;
    bool manual_light_;
    bool manual_heater_;
    bool manual_pump_;

    Channel temperature_;
    Channel air_humidity_;
    Channel soil_humidity_;
    Channel illuminance_;
    Channel water_level_;

    bool outputs_[4]; // [0]=fan [1]=light [2]=heater [3]=pump（硬件已确认的状态）
    bool desired_[4];             // 期望状态；与 outputs_ 不一致时每拍重试
    uint32_t last_output_try_ms_[4];
    uint32_t now_ms_;             // 当前拍的时间（供 setOutput 内部计时用）

    uint32_t boot_ms_;
    uint32_t last_step_ms_;
    uint32_t last_sample_ms_;
    bool started_;

    uint32_t last_heater_switch_ms_;
    uint32_t last_fan_switch_ms_;

    uint32_t pump_start_ms_;
    uint32_t pump_last_start_ms_;
    uint32_t pump_runs_today_;
    uint32_t pump_day_index_;
    bool pump_active_;
    bool pump_started_once_;
    bool pump_dose_started_;   // 泵真正通电后才开始计剂量时间
    uint32_t pump_applied_ms_;

    int16_t turn_angle_;
    int8_t turn_direction_;
    uint32_t last_turn_ms_;
    uint8_t turn_fail_count_;
    uint32_t turn_next_try_ms_;

    uint32_t light_on_since_ms_;

    uint32_t time_of_day_ms_;
    uint32_t time_base_ms_;
    bool time_valid_;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_LOGICCONTROLLER_H
