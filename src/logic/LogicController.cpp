//
// 温室控制逻辑实现。纯 C++ 子集：无 FSP、无 RTOS、无堆、无异常。
//

#include "logic/LogicController.h"

namespace greenhouse {
namespace {

constexpr uint32_t kDayMs = 24UL * 60UL * 60UL * 1000UL;
constexpr uint32_t kTurntableIdleCheckMs = 1000UL;

// 无符号时间比较：a 是否已到/超过 b（跨回绕安全）。
inline bool reached(uint32_t a, uint32_t b)
{
    return static_cast<int32_t>(a - b) >= 0;
}

inline uint32_t elapsed(uint32_t now, uint32_t since)
{
    return now - since;
}

} // namespace

LogicController::LogicController(IHardwareData &hw, const GreenhouseParams &params)
    : hw_(hw),
      params_(params),
      policy_(),
      fault_bits_(FAULT_NONE),
      mode_(OperatingMode::AUTO),
      manual_deadline_ms_(0UL),
      manual_fan_(false),
      manual_light_(false),
      manual_heater_(false),
      manual_pump_(false),
      outputs_{false, false, false, false},
      desired_{false, false, false, false},
      last_output_try_ms_{0UL, 0UL, 0UL, 0UL},
      now_ms_(0UL),
      boot_ms_(0UL),
      last_step_ms_(0UL),
      last_sample_ms_(0UL),
      started_(false),
      last_heater_switch_ms_(0UL),
      last_fan_switch_ms_(0UL),
      pump_start_ms_(0UL),
      pump_last_start_ms_(0UL),
      pump_runs_today_(0UL),
      pump_day_index_(0UL),
      pump_active_(false),
      pump_started_once_(false),
      pump_dose_started_(false),
      pump_applied_ms_(0UL),
      turn_angle_(0),
      turn_direction_(1),
      last_turn_ms_(0UL),
      turn_fail_count_(0U),
      turn_next_try_ms_(0UL),
      light_on_since_ms_(0UL),
      time_of_day_ms_(0UL),
      time_base_ms_(0UL),
      time_valid_(false)
{
    (void) normalizeParams(&params_);

    temperature_.value = 0.0F;
    temperature_.fail_count = 0U;
    temperature_.valid = false;
    air_humidity_ = temperature_;
    soil_humidity_ = temperature_;
    illuminance_ = temperature_;
    water_level_ = temperature_;

    snap_.temperature = 0.0F;
    snap_.air_humidity = 0.0F;
    snap_.soil_humidity = 0.0F;
    snap_.illuminance = 0U;
    snap_.has_enough_water = false;
    snap_.fan_on = false;
    snap_.light_on = false;
    snap_.heater_on = false;
    snap_.pump_on = false;
    snap_.turn_angle = 0;
    snap_.turntable_moving = false;
    snap_.faults = FAULT_NONE;
    snap_.mode = OperatingMode::AUTO;
    snap_.time_valid = false;
    snap_.uptime_ms = 0UL;
    snap_.pump_runs_today = 0UL;
}

// ---------------------------------------------------------------- 配置

void LogicController::setParams(const GreenhouseParams &params)
{
    params_ = params;
    (void) normalizeParams(&params_);
}

const GreenhouseParams &LogicController::params() const
{
    return params_;
}

void LogicController::setConflictPolicy(const ConflictPolicy &policy)
{
    policy_ = policy;
}

const ConflictPolicy &LogicController::conflictPolicy() const
{
    return policy_;
}

void LogicController::setMode(OperatingMode mode, uint32_t now_ms)
{
    mode_ = mode;
    manual_deadline_ms_ = now_ms + params_.manual_timeout_ms;
    manual_fan_ = false;
    manual_light_ = false;
    manual_heater_ = false;
    manual_pump_ = false;

    if (OperatingMode::AUTO == mode)
    {
        return;
    }

    // 手动/关闭：立即落安全态，避免上一个模式的残留动作
    stopPump();
    setOutput(OutputId::HEATER, false);
    if (OperatingMode::OFF == mode)
    {
        setOutput(OutputId::FAN, false);
        setOutput(OutputId::LIGHT, false);
    }
}

OperatingMode LogicController::mode() const
{
    return mode_;
}

bool LogicController::setManualOutput(OutputId id, bool on, uint32_t now_ms)
{
    if (OperatingMode::MANUAL != mode_)
    {
        return false;
    }

    manual_deadline_ms_ = now_ms + params_.manual_timeout_ms;

    switch (id)
    {
        case OutputId::FAN:
            manual_fan_ = on;
            break;
        case OutputId::LIGHT:
            manual_light_ = on;
            break;
        case OutputId::HEATER:
            if (on && !sensorOk(temperature_))
            {
                return false;
            }
            manual_heater_ = on;
            break;
        case OutputId::PUMP:
            if (on && !waterOk())
            {
                return false; // 缺水时拒绝启动，防止水泵空转
            }
            manual_pump_ = on;
            break;
        default:
            return false;
    }

    return true;
}

// ---------------------------------------------------------------- 时间

void LogicController::setTimeOfDay(uint32_t ms_of_day)
{
    time_of_day_ms_ = ms_of_day % kDayMs;
    time_base_ms_ = last_step_ms_;
    time_valid_ = true;
}

void LogicController::clearTimeOfDay()
{
    time_valid_ = false;
    time_of_day_ms_ = 0UL;
    time_base_ms_ = 0UL;
}

uint32_t LogicController::timeOfDay(uint32_t now_ms) const
{
    if (!time_valid_)
    {
        return 0UL;
    }
    return (time_of_day_ms_ + elapsed(now_ms, time_base_ms_)) % kDayMs;
}

uint32_t LogicController::dayIndex(uint32_t now_ms) const
{
    return now_ms / kDayMs;
}

bool LogicController::isDaytime(uint32_t now_ms) const
{
    if (!time_valid_)
    {
        return false;
    }

    const uint32_t tod = timeOfDay(now_ms);
    const uint32_t start = params_.day_start_ms;
    const uint32_t len = params_.day_length_ms;

    if ((start + len) <= kDayMs)
    {
        return (tod >= start) && (tod < (start + len));
    }

    // 跨零点
    return (tod >= start) || (tod < ((start + len) % kDayMs));
}

// ---------------------------------------------------------------- 主循环

void LogicController::step(uint32_t now_ms)
{
    now_ms_ = now_ms;

    if (!started_)
    {
        started_ = true;
        boot_ms_ = now_ms;
        last_step_ms_ = now_ms;
        last_sample_ms_ = now_ms;
        last_heater_switch_ms_ = now_ms - kMinSwitchMs;
        last_fan_switch_ms_ = now_ms - kMinSwitchMs;
        last_turn_ms_ = now_ms;
        sample(now_ms);
    }
    else if (sampleDue(now_ms))
    {
        last_sample_ms_ = now_ms;
        sample(now_ms);
    }

    evaluate(now_ms);

    // 上一拍有命令未被硬件接受（例如继电器拒动、关泵超时）时在这里补发。
    retryOutputs();

    snap_.faults = fault_bits_;
    snap_.mode = mode_;
    snap_.time_valid = time_valid_;
    snap_.uptime_ms = elapsed(now_ms, boot_ms_);
    snap_.turn_angle = turn_angle_;
    snap_.pump_runs_today = pump_runs_today_;
    snap_.pump_on = outputs_[static_cast<uint8_t>(OutputId::PUMP)];

    last_step_ms_ = now_ms;
}

const ControllerSnapshot &LogicController::snapshot() const
{
    return snap_;
}

uint32_t LogicController::faults() const
{
    return fault_bits_;
}

bool LogicController::sampleDue(uint32_t now_ms) const
{
    return elapsed(now_ms, last_sample_ms_) >= params_.sample_period_ms;
}

void LogicController::sample(uint32_t now_ms)
{
    (void) now_ms;

    float value = 0.0F;

    if (hw_.readTemperature(&value))
    {
        temperature_.value = value;
        temperature_.valid = true;
        temperature_.fail_count = 0U;
        snap_.temperature = value;
    }
    else if (temperature_.fail_count < 255U)
    {
        temperature_.fail_count++;
    }

    if (hw_.readAirHumidity(&value))
    {
        air_humidity_.value = value;
        air_humidity_.valid = true;
        air_humidity_.fail_count = 0U;
        snap_.air_humidity = value;
    }
    else if (air_humidity_.fail_count < 255U)
    {
        air_humidity_.fail_count++;
    }

    if (hw_.readSoilHumidity(&value))
    {
        soil_humidity_.value = value;
        soil_humidity_.valid = true;
        soil_humidity_.fail_count = 0U;
        snap_.soil_humidity = value;
    }
    else if (soil_humidity_.fail_count < 255U)
    {
        soil_humidity_.fail_count++;
    }

    uint16_t illuminance = 0U;
    if (hw_.readIlluminance(&illuminance))
    {
        illuminance_.value = static_cast<float>(illuminance);
        illuminance_.valid = true;
        illuminance_.fail_count = 0U;
        snap_.illuminance = illuminance;
    }
    else if (illuminance_.fail_count < 255U)
    {
        illuminance_.fail_count++;
    }

    bool has_enough_water = false;
    if (hw_.readWaterLevel(&has_enough_water))
    {
        water_level_.value = has_enough_water ? 1.0F : 0.0F;
        water_level_.valid = true;
        water_level_.fail_count = 0U;
        snap_.has_enough_water = has_enough_water;
    }
    else if (water_level_.fail_count < 255U)
    {
        water_level_.fail_count++;
    }
}

// ---------------------------------------------------------------- 决策

bool LogicController::sensorOk(const Channel &channel) const
{
    return channel.valid && (channel.fail_count < params_.sensor_fail_limit);
}

bool LogicController::waterOk() const
{
    return water_level_.valid && (water_level_.fail_count < params_.sensor_fail_limit) &&
           (water_level_.value >= 0.5F);
}

void LogicController::evaluate(uint32_t now_ms)
{
    const uint8_t limit = params_.sensor_fail_limit;

    reportFault(FAULT_TEMPERATURE_SENSOR, temperature_.valid && (temperature_.fail_count >= limit));
    reportFault(FAULT_AIR_HUMIDITY_SENSOR,
                air_humidity_.valid && (air_humidity_.fail_count >= limit));
    reportFault(FAULT_SOIL_HUMIDITY_SENSOR,
                soil_humidity_.valid && (soil_humidity_.fail_count >= limit));
    reportFault(FAULT_ILLUMINANCE_SENSOR,
                illuminance_.valid && (illuminance_.fail_count >= limit));
    // 湿度过低且没有加湿手段：只做诊断上报，不改变动作。
    // 带回滞：低于 (min - 回滞带) 才置位，回升到 min 才清除，避免阈值附近抖动。
    const bool humidity_low_active =
        sensorOk(air_humidity_) &&
        ((air_humidity_.value < (params_.min_air_humidity - params_.air_humidity_hysteresis)) ||
         (((fault_bits_ & FAULT_AIR_HUMIDITY_LOW) != 0U) &&
          (air_humidity_.value < params_.min_air_humidity)));
    reportFault(FAULT_AIR_HUMIDITY_LOW, humidity_low_active);
    reportFault(FAULT_TIME_UNSET, !time_valid_);

    // 水位通道失效时按"缺水"处理：宁可少浇，也不空转烧泵。
    const bool water_available = water_level_.valid && (water_level_.fail_count < limit);
    const bool water_is_lack = (!water_available) || (water_level_.value < 0.5F);
    reportFault(FAULT_WATER_LACK, water_is_lack);
    if (water_is_lack)
    {
        stopPump();
    }

    const bool over_temperature = sensorOk(temperature_) &&
                                  (temperature_.value > params_.max_temperature);
    reportFault(FAULT_OVER_TEMPERATURE, over_temperature);

    if (pump_active_ &&
        (elapsed(now_ms, pump_start_ms_) >= (params_.pump_dose_ms + params_.pump_dose_ms)))
    {
        reportFault(FAULT_PUMP_TIMEOUT, true); // 剂量保护都失效时的兜底
        stopPump();
    }

    // 手动模式超时 → 自动
    if ((OperatingMode::MANUAL == mode_) && reached(now_ms, manual_deadline_ms_))
    {
        mode_ = OperatingMode::AUTO;
    }

    if (OperatingMode::OFF == mode_)
    {
        applySafeState(now_ms);
        return;
    }

    if (OperatingMode::MANUAL == mode_)
    {
        setOutput(OutputId::FAN, manual_fan_ || over_temperature);
        setOutput(OutputId::LIGHT, manual_light_);
        setOutput(OutputId::HEATER,
                  manual_heater_ && !over_temperature && sensorOk(temperature_));

        if (manual_pump_ && !pump_active_)
        {
            startPump(now_ms);
        }
        else if (!manual_pump_ && pump_active_)
        {
            stopPump();
        }
        else if (pump_active_ && (elapsed(now_ms, pump_start_ms_) >= params_.pump_dose_ms))
        {
            stopPump();
        }
        return;
    }

    updateTemperatureControl(now_ms);
    updateAirHumidityControl(now_ms);
    updateIlluminance(now_ms);
    updateIrrigation(now_ms);
    updateTurntable(now_ms);

    // 各路过完自己的规则后，统一消解冲突（超温 / 低温高湿 / 功率预算）。
    applyConflictPolicy(now_ms);
}

void LogicController::applyConflictPolicy(uint32_t now_ms)
{
    const bool over_temperature = sensorOk(temperature_) &&
                                  (temperature_.value > params_.max_temperature);

    // 1) 超温：停加热、抑制补光、暂停浇水（补光与浇水都会加剧高温问题）
    if (over_temperature)
    {
        setOutput(OutputId::HEATER, false);

        if (policy_.suppress_light_on_over_temperature)
        {
            setOutput(OutputId::LIGHT, false);
        }
        if (policy_.pump_blocked_on_over_temperature)
        {
            stopPump();
        }
    }

    // 2) 低温高湿：加热与通风互斥，只有"湿度远超上限 + 脉冲窗口"才破例
    if (policy_.block_fan_while_heating &&
        outputs_[static_cast<uint8_t>(OutputId::HEATER)] &&
        outputs_[static_cast<uint8_t>(OutputId::FAN)])
    {
        const bool humidity_override =
            sensorOk(air_humidity_) &&
            (air_humidity_.value >=
             (params_.max_air_humidity + policy_.fan_humidity_override_pct));

        if (!(humidity_override && fanPulseOpen(now_ms)))
        {
            setOutput(OutputId::FAN, false);
        }
    }

    // 3) 加热与浇水互斥：浇水是短动作，让路给持续加热，下一拍再试
    if (policy_.heater_pump_mutex &&
        outputs_[static_cast<uint8_t>(OutputId::HEATER)] &&
        outputs_[static_cast<uint8_t>(OutputId::PUMP)])
    {
        stopPump();
    }

    // 4) 同时动作数量上限：按 加热 > 泵 > 风扇 > 补光 的优先级逐个让路
    uint8_t on_count = countOutputsOn();
    if (on_count > policy_.max_simultaneous_outputs)
    {
        const OutputId drop_order[] = {OutputId::LIGHT, OutputId::FAN, OutputId::PUMP};
        for (uint8_t i = 0U; i < 3U; ++i)
        {
            if (on_count <= policy_.max_simultaneous_outputs)
            {
                break;
            }
            const OutputId id = drop_order[i];
            if (outputs_[static_cast<uint8_t>(id)])
            {
                if (OutputId::PUMP == id)
                {
                    stopPump();
                }
                else
                {
                    setOutput(id, false);
                }
                on_count = countOutputsOn();
            }
        }
    }
}

bool LogicController::fanPulseOpen(uint32_t now_ms) const
{
    const uint32_t on = policy_.fan_pulse_on_ms;
    const uint32_t total = policy_.fan_pulse_interval_ms;

    if ((0UL == on) || (0UL == total) || (on >= total))
    {
        return true; // 配置成常开
    }

    const uint32_t phase = elapsed(now_ms, boot_ms_) % total;
    return phase < on;
}

uint8_t LogicController::countOutputsOn() const
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (outputs_[i])
        {
            count++;
        }
    }
    return count;
}

void LogicController::applySafeState(uint32_t now_ms)
{
    (void) now_ms;
    stopPump();
    setOutput(OutputId::HEATER, false);
    setOutput(OutputId::FAN, false);
    setOutput(OutputId::LIGHT, false);
}

void LogicController::updateTemperatureControl(uint32_t now_ms)
{
    if (!sensorOk(temperature_))
    {
        setOutput(OutputId::HEATER, false); // 无法测温就不加热
        return;
    }

    const float value = temperature_.value;
    const uint8_t index = static_cast<uint8_t>(OutputId::HEATER);
    bool want = outputs_[index];

    if (value > params_.max_temperature)
    {
        want = false;
    }
    else if (value < params_.min_temperature)
    {
        want = true;
    }
    else if (want && (value >= (params_.min_temperature + params_.temperature_hysteresis)))
    {
        want = false;
    }

    if (want != outputs_[index])
    {
        if (elapsed(now_ms, last_heater_switch_ms_) < kMinSwitchMs)
        {
            want = !want; // 间隔不足，本轮不切换
        }
        else
        {
            last_heater_switch_ms_ = now_ms;
        }
    }

    if (want != outputs_[index])
    {
        setOutput(OutputId::HEATER, want);
    }
}

void LogicController::updateAirHumidityControl(uint32_t now_ms)
{
    const uint8_t index = static_cast<uint8_t>(OutputId::FAN);

    // 超温是安全事件：直接开风扇降温，绕开 30 s 防抖。
    // （否则"刚关风扇 5 s 后升温"会白等半分钟，这是之前的缺陷。）
    if (sensorOk(temperature_) && (temperature_.value > params_.max_temperature))
    {
        if (!outputs_[index])
        {
            setOutput(OutputId::FAN, true);
            last_fan_switch_ms_ = now_ms;
        }
        return;
    }

    bool want = outputs_[index];

    if (sensorOk(air_humidity_))
    {
        if (air_humidity_.value >= params_.max_air_humidity)
        {
            want = true;
        }
        else if (want &&
                 (air_humidity_.value <= (params_.max_air_humidity - params_.air_humidity_hysteresis)))
        {
            want = false;
        }
    }
    else
    {
        want = false; // 湿度不可测时不做排湿
    }

    if (want != outputs_[index])
    {
        if (elapsed(now_ms, last_fan_switch_ms_) < kMinSwitchMs)
        {
            want = !want;
        }
        else
        {
            last_fan_switch_ms_ = now_ms;
        }
    }

    if (want != outputs_[index])
    {
        setOutput(OutputId::FAN, want);
    }
}

void LogicController::updateIlluminance(uint32_t now_ms)
{
    if (!sensorOk(illuminance_))
    {
        setOutput(OutputId::LIGHT, false);
        return;
    }

    const bool light_on = outputs_[static_cast<uint8_t>(OutputId::LIGHT)];
    bool want = light_on;

    // 没有时间基准时按策略退化：允许补光则忽略昼夜，只按光照阈值。
    const bool daytime = time_valid_ ? isDaytime(now_ms) : policy_.allow_light_without_time;

    if (!daytime)
    {
        // 夜间不补光；但满足最短点亮时间后才关，避免频繁通断
        if (light_on && (elapsed(now_ms, light_on_since_ms_) < params_.illuminance_min_on_ms))
        {
            want = true;
        }
        else
        {
            want = false;
        }
    }
    else if (illuminance_.value < static_cast<float>(params_.min_illuminance))
    {
        want = true;
    }
    else if (light_on && (elapsed(now_ms, light_on_since_ms_) < params_.illuminance_min_on_ms))
    {
        want = true;
    }
    else
    {
        want = false;
    }

    if (want != light_on)
    {
        if (want)
        {
            light_on_since_ms_ = now_ms;
        }
        setOutput(OutputId::LIGHT, want);
    }
}

bool LogicController::pumpAllowed(uint32_t now_ms)
{
    if (!waterOk())
    {
        return false;
    }

    const uint32_t today = dayIndex(now_ms);
    if (today != pump_day_index_)
    {
        pump_day_index_ = today;
        pump_runs_today_ = 0UL;
    }

    if (pump_runs_today_ >= params_.pump_max_per_day)
    {
        return false;
    }

    if (pump_started_once_ &&
        (elapsed(now_ms, pump_last_start_ms_) < params_.pump_min_interval_ms))
    {
        return false;
    }

    return true;
}

void LogicController::startPump(uint32_t now_ms)
{
    if (pump_active_ || !pumpAllowed(now_ms))
    {
        return;
    }

    pump_active_ = true;
    pump_started_once_ = true;
    pump_dose_started_ = false;
    pump_start_ms_ = now_ms;
    pump_last_start_ms_ = now_ms;

    setOutput(OutputId::PUMP, true);
    // 只有硬件确认通电后才计剂量时间与次数；否则由 retryOutputs() 继续补发。
}

void LogicController::stopPump()
{
    if (!pump_active_ && !outputs_[static_cast<uint8_t>(OutputId::PUMP)])
    {
        return;
    }
    pump_active_ = false;
    pump_dose_started_ = false;
    setOutput(OutputId::PUMP, false);
}

void LogicController::updateIrrigation(uint32_t now_ms)
{
    if (pump_active_)
    {
        if (!waterOk())
        {
            stopPump();
            return;
        }

        if (!outputs_[static_cast<uint8_t>(OutputId::PUMP)])
        {
            return; // 还没通电，等 retryOutputs() 补发
        }

        // 土壤已经浇透（可能是传感器响应或上一轮残留水分）就提前收手，避免浇过头。
        if (sensorOk(soil_humidity_) &&
            (soil_humidity_.value >= params_.max_soil_humidity))
        {
            stopPump();
            return;
        }

        if (elapsed(now_ms, pump_applied_ms_) >= params_.pump_dose_ms)
        {
            stopPump();
        }
        return;
    }

    if (!sensorOk(soil_humidity_))
    {
        return; // 土壤湿度不可测时不浇水
    }

    if (soil_humidity_.value <= params_.min_soil_humidity)
    {
        startPump(now_ms);
    }
}

void LogicController::updateTurntable(uint32_t now_ms)
{
    if (!time_valid_)
    {
        return; // 没有时间基准就不转，避免昼夜逻辑错乱
    }

    // 失败退避：上一次动作失败后 60 s 内不再试，避免每秒重试把电机/驱动憋坏。
    if (!reached(now_ms, turn_next_try_ms_))
    {
        return;
    }

    if (!isDaytime(now_ms))
    {
        if ((turn_angle_ != 0) && (elapsed(now_ms, last_turn_ms_) >= kTurntableIdleCheckMs))
        {
            const int16_t delta = static_cast<int16_t>(-turn_angle_);
            if (hw_.rotateTurntable(delta))
            {
                turn_angle_ = 0;
                turn_fail_count_ = 0U;
                turn_next_try_ms_ = 0UL;
                reportFault(FAULT_TURNTABLE_FAILED, false);
            }
            else
            {
                turn_fail_count_++;
                turn_next_try_ms_ = now_ms + kTurnRetryMs;
                reportFault(FAULT_TURNTABLE_FAILED, (turn_fail_count_ >= kTurnFailLimit));
            }
            last_turn_ms_ = now_ms;
        }
        return;
    }

    if (elapsed(now_ms, last_turn_ms_) < params_.turn_interval_ms)
    {
        return;
    }

    last_turn_ms_ = now_ms;

    const int16_t step = params_.turn_step_angle;
    const int16_t limit = params_.max_turn_angle;

    int16_t next = static_cast<int16_t>(turn_angle_ + static_cast<int16_t>(turn_direction_ * step));
    if ((next > limit) || (next < static_cast<int16_t>(-limit)))
    {
        turn_direction_ = static_cast<int8_t>(-turn_direction_);
        next = static_cast<int16_t>(turn_angle_ + static_cast<int16_t>(turn_direction_ * step));
    }

    if (next > limit)
    {
        next = limit;
    }
    if (next < static_cast<int16_t>(-limit))
    {
        next = static_cast<int16_t>(-limit);
    }

    const int16_t delta = static_cast<int16_t>(next - turn_angle_);
    if (0 == delta)
    {
        return;
    }

    snap_.turntable_moving = true;
    if (hw_.rotateTurntable(delta))
    {
        turn_angle_ = next;
        turn_fail_count_ = 0U;
        turn_next_try_ms_ = 0UL;
        reportFault(FAULT_TURNTABLE_FAILED, false);
    }
    else
    {
        turn_fail_count_++;
        turn_next_try_ms_ = now_ms + kTurnRetryMs;
        reportFault(FAULT_TURNTABLE_FAILED, (turn_fail_count_ >= kTurnFailLimit));
    }
    snap_.turntable_moving = false;
}

// ---------------------------------------------------------------- 输出

uint8_t LogicController::indexOf(OutputId id)
{
    const uint8_t index = static_cast<uint8_t>(id);
    return (index > 3U) ? 0U : index;
}

void LogicController::setOutput(OutputId id, bool on)
{
    // 记录"期望状态"，再尝试下发一次。下发失败不必立刻处理：
    // retryOutputs() 会在后续 step() 里按 kOutputRetryMs 继续补发，
    // 这样"关泵被拒"不会导致泵一直转着没人管。
    const uint8_t index = indexOf(id);

    desired_[index] = on;

    if (outputs_[index] == on)
    {
        return;
    }

    bool accepted = false;
    switch (id)
    {
        case OutputId::FAN:
            accepted = hw_.setFanStatus(on);
            break;
        case OutputId::LIGHT:
            accepted = hw_.setLightStatus(on);
            break;
        case OutputId::HEATER:
            accepted = hw_.setHeaterStatus(on);
            break;
        case OutputId::PUMP:
            accepted = hw_.setPumpStatus(on);
            break;
        default:
            return;
    }

    last_output_try_ms_[index] = now_ms_;

    if (!accepted)
    {
        return;
    }

    outputs_[index] = on;
    if (OutputId::PUMP == id)
    {
        if (on && pump_active_ && !pump_dose_started_)
        {
            // 泵真正通电后才开始计剂量、才占用当日浇水次数。
            pump_dose_started_ = true;
            pump_applied_ms_ = now_ms_;
            pump_runs_today_++;
        }
        if (!on)
        {
            pump_dose_started_ = false;
        }
    }

    switch (id)
    {
        case OutputId::FAN:
            snap_.fan_on = on;
            break;
        case OutputId::LIGHT:
            snap_.light_on = on;
            break;
        case OutputId::HEATER:
            snap_.heater_on = on;
            break;
        case OutputId::PUMP:
            snap_.pump_on = on;
            break;
        default:
            break;
    }
}

void LogicController::retryOutputs()
{
    for (uint8_t i = 0U; i < 4U; ++i)
    {
        if (desired_[i] == outputs_[i])
        {
            continue;
        }
        if (elapsed(now_ms_, last_output_try_ms_[i]) < kOutputRetryMs)
        {
            continue;
        }

        const OutputId id = static_cast<OutputId>(i);
        // setOutput 会重新记录 desired 并尝试下发一次
        setOutput(id, desired_[i]);
    }
}

void LogicController::reportFault(uint32_t bits, bool active)
{
    if (active)
    {
        fault_bits_ |= bits;
    }
    else
    {
        fault_bits_ &= ~bits;
    }
}

} // namespace greenhouse
