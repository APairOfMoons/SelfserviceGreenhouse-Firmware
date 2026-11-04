//
// 逻辑层 PC 端测试（不进固件）。
//
// 用法（在仓库根目录，任意主机编译器）：
//   g++ -std=c++17 -DLOGIC_HOST_TEST -Isrc src/logic/LogicController.cpp
//       src/logic/GreenhouseParams.cpp src/logic/FakeHardware.cpp
//       src/logic/VariableJudge.cpp src/logic/ExceptionMapping.cpp
//       src/logic/MonitoringDataMapper.cpp src/logic/TelemetryReporter.cpp
//       src/logic/test/LogicControllerHostTest.cpp -o logic_test.exe
//   .\logic_test.exe
//
// 未定义 LOGIC_HOST_TEST 时本文件编译为空，不影响固件构建。
//

#if defined(LOGIC_HOST_TEST)

#include <cstdio>
#include <limits>

#include "app/ExceptionMapping.h"
#include "logic/FakeHardware.h"
#include "configuration/LogicalConfiguration.h"
#include "logic/LogicController.h"
#include "app/MonitoringDataMapper.h"
#include "logic/TelemetryReporter.h"
#include "logic/VariableJudge.h"

namespace {

int g_checks = 0;
int g_failures = 0;

void checkImpl(bool ok, const char *expr, const char *file, int line)
{
    ++g_checks;
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL %s:%d  %s\n", file, line, expr);
    }
}

#define CHECK(cond) checkImpl((cond), #cond, __FILE__, __LINE__)

constexpr uint32_t kStepMs = 100UL;
constexpr uint32_t kSecond = 1000UL;
constexpr uint32_t kMinute = 60UL * kSecond;
constexpr uint32_t kHour = 60UL * kMinute;

// 测试自带的参数集：logic/ 不再内置默认值，数值由调用方提供。
LogicalStatics fastParams()
{
    LogicalStatics params = {};

    params.variable.minTemperature = 18.0F;
    params.variable.maxTemperature = 28.0F;
    params.variable.temperatureHysteresis = 1.5F;

    params.variable.minAirHumidity = 30.0F;
    params.variable.maxAirHumidity = 75.0F;
    params.variable.airHumidityHysteresis = 5.0F;

    params.variable.minIlluminance = 1200U;
    params.variable.illuminanceMinOnMs = 10UL * kMinute;
    params.variable.dayStartMs = 7UL * kHour;
    params.variable.dayLengthMs = 10UL * kHour;

    params.variable.minSoilHumidity = 35.0F;
    params.variable.maxSoilHumidity = 55.0F;
    params.variable.pumpDurationMs = 800UL;
    params.variable.pumpMinIntervalMs = 6UL * kHour;
    params.variable.pumpMaxPerDay = 3U;

    params.variable.maxTurnAngle = 60;
    params.variable.turnStepAngle = 15;
    params.variable.turnIntervalMs = 60UL * kSecond;
    params.variable.turnAnglePerDay = 120;

    params.variable.samplePeriodMs = kStepMs;
    params.fixed.sensorFailLimit = 3U;
    params.fixed.manualTimeoutMs = 60UL * kSecond;
    params.fixed.hasRtc = true;

    return params;
}

struct Harness
{
    greenhouse::FakeHardware hw;
    LogicalStatics params;
    greenhouse::LogicController ctrl;
    uint32_t now;

    Harness() : hw(), params(fastParams()), ctrl(hw, params), now(1000U) {}

    void advance(uint32_t duration_ms)
    {
        const uint32_t end = now + duration_ms;
        while (now < end)
        {
            ctrl.step(now);
            now += kStepMs;
        }
    }
};

// 1) 上电即安全态：所有执行器默认关闭
void testPowerOnIsSafe()
{
    Harness h;
    h.ctrl.step(h.now);

    CHECK(!h.hw.fan_on);
    CHECK(!h.hw.light_on);
    CHECK(!h.hw.heater_on);
    CHECK(!h.hw.pump_on);
    CHECK(h.ctrl.snapshot().uptime_ms == 0UL);
}

// 2) 低温开加热，回到回滞带上沿后关闭
void testHeaterHysteresis()
{
    Harness h;
    h.hw.temperature = 15.0F;
    h.advance(2UL * kSecond);
    CHECK(h.hw.heater_on);

    h.hw.temperature = 26.0F; // > min(18) + hysteresis(1.5)
    h.advance(2UL * kMinute);
    CHECK(!h.hw.heater_on);

    h.hw.temperature = 18.8F; // 在回滞带内，不应重新加热
    h.advance(2UL * kMinute);
    CHECK(!h.hw.heater_on);
}

// 3) 湿度超限开风扇，回落后关闭
void testFanOnHighAirHumidity()
{
    Harness h;
    h.hw.air_humidity = 90.0F;
    h.advance(2UL * kSecond);
    CHECK(h.hw.fan_on);

    h.hw.air_humidity = 60.0F; // < max(75) - hysteresis(5)
    h.advance(2UL * kMinute);
    CHECK(!h.hw.fan_on);
}

// 4) 土壤干旱时定量浇水，冷却间隔内不重复
void testIrrigationDoseAndCooldown()
{
    Harness h;
    h.hw.soil_humidity = 20.0F;

    h.advance(300UL); // 单次剂量 800 ms，此时泵应在运行
    CHECK(h.hw.pump_on);

    h.advance(2UL * kSecond); // 剂量结束
    CHECK(!h.hw.pump_on);
    CHECK(h.ctrl.snapshot().pump_runs_today == 1UL);

    h.advance(10UL * kMinute); // 6 小时冷却期内不再启动
    CHECK(!h.hw.pump_on);
    CHECK(h.ctrl.snapshot().pump_runs_today == 1UL);
}

// 5) 缺水立即停泵，且拒绝手动启动
void testWaterLackStopsPump()
{
    Harness h;
    h.hw.soil_humidity = 10.0F;
    h.advance(300UL);
    CHECK(h.hw.pump_on);

    h.hw.has_enough_water = false;
    h.advance(300UL);
    CHECK(!h.hw.pump_on);
    CHECK((h.ctrl.faults() & greenhouse::FAULT_WATER_LACK) != 0U);

    h.ctrl.setMode(greenhouse::OperatingMode::MANUAL, h.now);
    CHECK(!h.ctrl.setManualOutput(greenhouse::OutputId::PUMP, true, h.now));
}

// 6) 温度传感器连续失败 → 报故障并停加热
void testTemperatureSensorFault()
{
    Harness h;
    h.hw.temperature = 10.0F;
    h.advance(2UL * kSecond);
    CHECK(h.hw.heater_on);

    h.hw.fail_temperature = true;
    h.advance(3UL * kSecond);
    CHECK((h.ctrl.faults() & greenhouse::FAULT_TEMPERATURE_SENSOR) != 0U);
    CHECK(!h.hw.heater_on);
}

// 7) 超温联锁：停加热并强制通风
void testOverTemperatureInterlock()
{
    Harness h;
    h.hw.temperature = 35.0F;
    h.advance(3UL * kSecond);

    CHECK(!h.hw.heater_on);
    CHECK(h.hw.fan_on);
    CHECK((h.ctrl.faults() & greenhouse::FAULT_OVER_TEMPERATURE) != 0U);
}

// 8) 补光只在白天；夜间且已满足最短点亮时间后关闭
void testLightOnlyInDaytime()
{
    Harness h;
    h.hw.illuminance = 100U;

    h.ctrl.setTimeOfDay(12UL * kHour);
    h.advance(2UL * kSecond);
    CHECK(h.hw.light_on);

    h.ctrl.setTimeOfDay(23UL * kHour);
    h.advance(2UL * kSecond); // 最短点亮 10 min 未到
    CHECK(h.hw.light_on);

    h.advance(11UL * kMinute);
    CHECK(!h.hw.light_on);
}

// 9) 白天摆动、夜间回中位
void testTurntableSwingAndPark()
{
    Harness h;
    h.ctrl.setTimeOfDay(9UL * kHour);
    h.advance(2UL * kSecond);
    CHECK(h.hw.turntable_angle == 0);

    h.advance(61UL * kSecond); // 摆动周期 60 s
    CHECK(h.hw.turntable_angle > 0);

    h.ctrl.setTimeOfDay(22UL * kHour);
    h.advance(3UL * kSecond);
    CHECK(h.hw.turntable_angle == 0);
}

// 10) 手动模式超时自动回到 AUTO
void testManualModeTimeout()
{
    Harness h;
    h.ctrl.setMode(greenhouse::OperatingMode::MANUAL, h.now);
    CHECK(h.ctrl.setManualOutput(greenhouse::OutputId::FAN, true, h.now));

    h.advance(2UL * kSecond);
    CHECK(h.hw.fan_on);
    CHECK(h.ctrl.mode() == greenhouse::OperatingMode::MANUAL);

    h.advance(61UL * kSecond);
    CHECK(h.ctrl.mode() == greenhouse::OperatingMode::AUTO);
}

// 11) 运行时可整体替换参数（上位机改参），新阈值立即生效
void testRuntimeParamChange()
{
    Harness h;
    h.hw.temperature = 12.0F;

    h.advance(2UL * kSecond);
    CHECK(h.hw.heater_on); // 默认下限 18 ℃

    LogicalStatics tuned = h.params;
    tuned.variable.minTemperature = 10.0F;
    h.ctrl.setConfiguration(tuned);

    h.advance(2UL * kMinute);
    CHECK(!h.hw.heater_on); // 新下限 10 ℃，12 ℃ 不再需要加热
}

// 12) 执行器拒绝命令时，本地状态不假装成功
void testActuatorRejectionKeepsState()
{
    Harness h;
    h.hw.temperature = 5.0F;
    h.hw.reject_actuators = true;

    h.advance(2UL * kSecond);
    CHECK(!h.hw.heater_on);
    CHECK(!h.ctrl.snapshot().heater_on);
}

// ---------------------------------------------------------------- 组合工况冲突

// 超温时即使很暗也不补光（补光灯是热源）
void testOverTempSuppressesLight()
{
    Harness h;
    h.hw.temperature = 35.0F;
    h.hw.illuminance = 100U;
    h.ctrl.setTimeOfDay(12UL * kHour);
    h.advance(2UL * kSecond);

    CHECK(h.hw.fan_on);
    CHECK(!h.hw.light_on);
}

// 低温 + 高湿：加热优先，风扇不能常开（否则热量被排走）
void testLowTempHighHumidityBlocksFan()
{
    Harness h;
    h.hw.temperature = 15.0F;
    h.hw.air_humidity = 90.0F; // 上限 75 + 10 = 85，90 只允许脉冲通风

    h.advance(400UL * kSecond); // 超过脉冲开启窗口(120 s)
    CHECK(h.hw.heater_on);
    CHECK(!h.hw.fan_on);
}

// 低温 + 极高湿度：脉冲窗口内允许通风
void testLowTempVeryHighHumidityPulsesFan()
{
    Harness h;
    h.hw.temperature = 15.0F;
    h.hw.air_humidity = 95.0F;

    h.advance(2UL * kSecond); // 脉冲开启段内
    CHECK(h.hw.heater_on);
    CHECK(h.hw.fan_on);

    h.advance(400UL * kSecond); // 脉冲关闭段
    CHECK(!h.hw.fan_on);
}

// 超温时暂停浇水
void testOverTempBlocksPump()
{
    Harness h;
    h.hw.temperature = 35.0F;
    h.hw.soil_humidity = 20.0F;

    h.advance(300UL);
    CHECK(!h.hw.pump_on);
}

// 功率预算：同时最多 1 路动作时，保留加热
void testPowerBudgetKeepsHeater()
{
    Harness h;
    greenhouse::ConflictPolicy policy;
    policy.max_simultaneous_outputs = 1U;
    h.ctrl.setConflictPolicy(policy);

    h.hw.temperature = 15.0F;
    h.hw.air_humidity = 95.0F;
    h.hw.soil_humidity = 20.0F;
    h.hw.illuminance = 100U;
    h.ctrl.setTimeOfDay(12UL * kHour);

    h.advance(2UL * kSecond);

    const int on_count = (h.hw.fan_on ? 1 : 0) + (h.hw.light_on ? 1 : 0) +
                         (h.hw.heater_on ? 1 : 0) + (h.hw.pump_on ? 1 : 0);
    CHECK(on_count == 1);
    CHECK(h.hw.heater_on);
}

// 湿度过低且无法纠正：只置诊断位，不做动作
void testLowHumidityIsDiagnosed()
{
    Harness h;
    h.hw.air_humidity = 20.0F; // 下限 30
    h.advance(2UL * kSecond);

    CHECK((h.ctrl.faults() & greenhouse::FAULT_AIR_HUMIDITY_LOW) != 0U);
    CHECK(!h.hw.fan_on);
}

// 超温必须绕过 30 s 防抖立即通风
void testOverTempBypassesFanGate()
{
    Harness h;
    h.hw.temperature = 25.0F;
    h.hw.air_humidity = 90.0F;
    h.advance(40UL * kSecond);
    CHECK(h.hw.fan_on);

    h.hw.air_humidity = 50.0F; // 湿度回落 → 风扇关
    h.advance(2UL * kSecond);
    CHECK(!h.hw.fan_on);

    h.hw.temperature = 36.0F; // 紧接着超温
    h.advance(200UL);
    CHECK(h.hw.fan_on);
}

// 关泵被拒时会被重试（缺水场景不能一直转泵）
void testPumpOffRetried()
{
    Harness h;
    h.hw.soil_humidity = 20.0F;
    h.advance(200UL);
    CHECK(h.hw.pump_on);

    h.hw.reject_pump_off = true;
    h.hw.has_enough_water = false;
    h.advance(500UL);
    CHECK(h.hw.pump_on); // 关不掉
    CHECK((h.ctrl.faults() & greenhouse::FAULT_WATER_LACK) != 0U);

    h.hw.reject_pump_off = false;
    h.advance(2UL * kSecond); // 重试成功
    CHECK(!h.hw.pump_on);
}

// 浇到"已浇透"阈值就提前停
void testPumpStopsWhenSoilWet()
{
    Harness h;
    h.hw.soil_humidity = 20.0F;
    h.advance(300UL);
    CHECK(h.hw.pump_on);

    h.hw.soil_humidity = 60.0F;
    h.advance(300UL);
    CHECK(!h.hw.pump_on);
}

// 低湿诊断位带回滞，不在阈值附近抖动
void testLowHumidityHysteresis()
{
    Harness h;
    h.hw.air_humidity = 20.0F;
    h.advance(200UL);
    CHECK((h.ctrl.faults() & greenhouse::FAULT_AIR_HUMIDITY_LOW) != 0U);

    h.hw.air_humidity = 29.0F; // 低于下限但高于 min-回滞，保持置位
    h.advance(200UL);
    CHECK((h.ctrl.faults() & greenhouse::FAULT_AIR_HUMIDITY_LOW) != 0U);

    h.hw.air_humidity = 31.0F; // 回到下限以上，清除
    h.advance(200UL);
    CHECK((h.ctrl.faults() & greenhouse::FAULT_AIR_HUMIDITY_LOW) == 0U);
}

// 无时间基准时补光按策略退化
void testLightWithoutTimeBase()
{
    Harness h;
    h.hw.illuminance = 100U;
    h.advance(2UL * kSecond);
    CHECK(h.hw.light_on); // 默认 allow_light_without_time = true

    Harness h2;
    greenhouse::ConflictPolicy policy;
    policy.allow_light_without_time = false;
    h2.ctrl.setConflictPolicy(policy);
    h2.hw.illuminance = 100U;
    h2.advance(2UL * kSecond);
    CHECK(!h2.hw.light_on);
}

// 转盘失败后按 60 s 退避，不再每秒重试
void testTurntableBackoff()
{
    Harness h;
    h.ctrl.setTimeOfDay(9UL * kHour);
    h.advance(61UL * kSecond);
    CHECK(h.hw.turntable_angle > 0);

    h.hw.fail_turntable = true;
    h.ctrl.setTimeOfDay(22UL * kHour);
    h.advance(2UL * kSecond);
    const uint32_t attempts = h.hw.turntable_attempts;
    CHECK(attempts >= 1U);

    h.advance(10UL * kSecond); // 退避期内不再尝试
    CHECK(h.hw.turntable_attempts == attempts);

    h.advance(60UL * kSecond); // 退避结束，允许再试一次
    CHECK(h.hw.turntable_attempts == attempts + 1U);
}

// ---------------------------------------------------------------- 四路量判定

// 测试用的逻辑配置（与默认值一致）。
LogicalStatics makeTestConfiguration()
{
    LogicalStatics config = {};
    config.variable.turnAnglePerDay = 10;
    config.variable.minTemperature = 10.0;
    config.variable.maxTemperature = 30.0;
    config.variable.temperatureHysteresis = 1.5;
    config.variable.minAirHumidity = 30.0;
    config.variable.maxAirHumidity = 90.0;
    config.variable.airHumidityHysteresis = 5.0;
    config.variable.minSoilHumidity = 30.0;
    config.variable.maxSoilHumidity = 55.0;
    config.variable.minIlluminance = 1200.0;
    config.variable.maxIlluminance = 100000.0;
    config.variable.illuminanceMinOnMs = 10UL * kMinute;
    config.variable.dayStartMs = 7UL * kHour;
    config.variable.dayLengthMs = 10UL * kHour;
    config.variable.pumpDurationMs = 800UL;
    config.variable.pumpMinIntervalMs = 6UL * kHour;
    config.variable.pumpMaxPerDay = 3U;
    config.variable.maxTurnAngle = 60;
    config.variable.turnStepAngle = 15;
    config.variable.turnIntervalMs = 20UL * kMinute;
    config.variable.samplePeriodMs = kStepMs;
    config.fixed.sensorFailLimit = 3U;
    config.fixed.manualTimeoutMs = 60UL * kSecond;
    config.fixed.hasRtc = true;
    return config;
}

void testVariableJudgeBasics()
{
    greenhouse::VariableJudge judge(makeTestConfiguration());
    CHECK(!judge.usedFallbackLimits());

    using greenhouse::VariableReading;
    using greenhouse::VarId;
    using greenhouse::VarState;

    CHECK(judge.judge(VarId::TEMPERATURE, {true, 25.0F}).state == VarState::NORMAL);
    CHECK(judge.judge(VarId::TEMPERATURE, {true, 5.0F}).state == VarState::LOW);
    CHECK(judge.judge(VarId::TEMPERATURE, {true, 35.0F}).state == VarState::HIGH);
    CHECK(judge.judge(VarId::TEMPERATURE, {false, 0.0F}).state == VarState::FAULT);

    CHECK(judge.judge(VarId::AIR_HUMIDITY, {true, 60.0F}).state == VarState::NORMAL);
    CHECK(judge.judge(VarId::AIR_HUMIDITY, {true, 20.0F}).state == VarState::LOW);
    CHECK(judge.judge(VarId::AIR_HUMIDITY, {true, 95.0F}).state == VarState::HIGH);
}

void testVariableJudgeHysteresis()
{
    greenhouse::VariableJudge judge(makeTestConfiguration());

    using greenhouse::VarId;
    using greenhouse::VarState;

    // 温度 10 ℃ 为下限，回滞带宽 0.5 ℃
    CHECK(judge.judge(VarId::TEMPERATURE, {true, 9.0F}).state == VarState::LOW);
    CHECK(judge.judge(VarId::TEMPERATURE, {true, 10.2F}).state == VarState::LOW);
    CHECK(judge.judge(VarId::TEMPERATURE, {true, 11.6F}).state == VarState::NORMAL);

    // 空气湿度 90 % 为上限，回滞带宽 2 %
    CHECK(judge.judge(VarId::AIR_HUMIDITY, {true, 95.0F}).state == VarState::HIGH);
    CHECK(judge.judge(VarId::AIR_HUMIDITY, {true, 88.5F}).state == VarState::HIGH);
    CHECK(judge.judge(VarId::AIR_HUMIDITY, {true, 84.0F}).state == VarState::NORMAL);
}

void testVariableJudgeMaskAndOverride()
{
    greenhouse::VariableJudge judge(makeTestConfiguration());

    greenhouse::EnvironmentSample sample = {};
    sample.temperature = {true, 5.0F};    // LOW  -> bit0
    sample.air_humidity = {true, 60.0F};  // NORMAL
    sample.soil_humidity = {true, 20.0F}; // LOW  -> bit2
    sample.illuminance = {true, 500.0F};  // LOW  -> bit3

    judge.judgeAll(sample);
    CHECK(judge.anyOutOfRange());
    CHECK(judge.outOfRangeMask() == 0x0DU);

    // 覆盖土壤阈值后，20 % 应落入新区间
    judge.setLimits(greenhouse::VarId::SOIL_HUMIDITY, {10.0F, 60.0F, 2.0F});
    CHECK(judge.judge(greenhouse::VarId::SOIL_HUMIDITY, {true, 20.0F}).state ==
          greenhouse::VarState::NORMAL);

    // 全部正常时掩码为 0
    judge.judge(greenhouse::VarId::TEMPERATURE, {true, 22.0F});
    judge.judge(greenhouse::VarId::ILLUMINANCE, {true, 8000.0F});
    CHECK(judge.outOfRangeMask() == 0x00U);
    CHECK(!judge.anyOutOfRange());
}

// ---------------------------------------------------------------- 上报转换

bool nearly(double a, double b, double eps)
{
    double delta = a - b;
    if (delta < 0.0)
    {
        delta = -delta;
    }
    return delta <= eps;
}

void testSnapshotToMonitoringData()
{
    Harness h;
    h.hw.temperature = 15.0F;
    h.hw.air_humidity = 90.0F;
    h.hw.soil_humidity = 44.0F;
    h.hw.illuminance = 100U;
    h.hw.has_enough_water = true;

    h.ctrl.setTimeOfDay(12UL * kHour);
    h.advance(2UL * kSecond);

    MonitoringData data = {};
    CHECK(greenhouse::toMonitoringData(h.ctrl.snapshot(), &data));

    CHECK(nearly(data.temperature, 15.0, 0.001));
    CHECK(nearly(data.air_humidity, 90.0, 0.001));
    CHECK(nearly(data.soil_humidity, 44.0, 0.001));
    CHECK(data.illuminance == 100);
    CHECK(data.has_enough_water);

    CHECK(data.heater_on); // 15 ℃ < 下限 18 ℃
    CHECK(data.fan_on);    // 90 % >= 上限 75 %
    CHECK(data.light_on);  // 白天且 100 lx < 1200 lx

    // 光照 16 位满量程不截断
    h.hw.illuminance = 65535U;
    h.advance(2UL * kSecond);
    CHECK(greenhouse::toMonitoringData(h.ctrl.snapshot(), &data));
    CHECK(data.illuminance == 65535);

    // 空指针保护
    CHECK(!greenhouse::toMonitoringData(h.ctrl.snapshot(), nullptr));
}

// ---------------------------------------------------------------- 上报节律

class FakeTelemetrySink : public greenhouse::ITelemetrySink
{
    public:
    bool accept;
    uint32_t calls;
    MonitoringData last;

    FakeTelemetrySink() : accept(true), calls(0U), last() {}

    bool publish(const MonitoringData &data) override
    {
        calls++;
        if (!accept)
        {
            return false;
        }
        last = data;
        return true;
    }
};

greenhouse::ReporterConfig makeReporterConfig()
{
    greenhouse::ReporterConfig cfg = {};
    cfg.heartbeat_ms = 30UL * kSecond;
    cfg.min_interval_ms = 1UL * kSecond;
    cfg.temperature_delta = 0.5F;
    cfg.air_humidity_delta = 2.0F;
    cfg.soil_humidity_delta = 1.0F;
    cfg.illuminance_delta = 50;
    cfg.report_on_actuator_change = true;
    return cfg;
}

void testTelemetryReporterCadence()
{
    FakeTelemetrySink sink;
    Harness h;
    greenhouse::TelemetryReporter reporter(sink, makeReporterConfig());

    // 首帧立即上报
    h.advance(100UL);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(reporter.hasPublished());
    CHECK(sink.calls == 1UL);

    // 限流期内不重复
    h.advance(500UL);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(sink.calls == 1UL);

    // 温度变化超过阈值 -> 立即上报
    h.hw.temperature = 26.0F;
    h.advance(2UL * kSecond);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(sink.calls == 2UL);
    CHECK(nearly(sink.last.temperature, 26.0, 0.001));

    // 无变化时按心跳上报
    const uint32_t before_heartbeat = sink.calls;
    h.advance(31UL * kSecond);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(sink.calls == before_heartbeat + 1UL);

    // 发送失败只计数，不排队
    sink.accept = false;
    h.advance(31UL * kSecond);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(reporter.failedCount() >= 1UL);
    CHECK(sink.calls == before_heartbeat + 2UL);

    // 链路恢复：reset 后补发一帧
    sink.accept = true;
    reporter.reset();
    CHECK(!reporter.hasPublished());
    h.advance(200UL);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(reporter.hasPublished());
    CHECK(reporter.publishedCount() >= 2UL);
}

void testTelemetryReporterActuatorChange()
{
    FakeTelemetrySink sink;

    greenhouse::ReporterConfig cfg = makeReporterConfig();
    cfg.heartbeat_ms = 3600UL * kSecond; // 屏蔽心跳
    cfg.temperature_delta = 100.0F;     // 屏蔽环境量变化
    cfg.air_humidity_delta = 100.0F;
    cfg.soil_humidity_delta = 100.0F;
    cfg.illuminance_delta = 100000;

    Harness h;
    greenhouse::TelemetryReporter reporter(sink, cfg);

    reporter.update(h.now, h.ctrl.snapshot()); // 首帧
    CHECK(sink.calls == 1UL);

    h.hw.temperature = 15.0F; // 触发加热（15 ℃ < 下限）
    h.advance(2UL * kSecond);
    reporter.update(h.now, h.ctrl.snapshot());
    CHECK(sink.calls == 2UL);
    CHECK(sink.last.heater_on);
}

// ---------------------------------------------------------------- 异常映射

void testExceptionMapping()
{
    CHECK(!greenhouse::toGreenhouseException(0U, nullptr));

    GreenhouseException exception = GreenhouseException::TURNTABLE_FAILED_EXCEPTION;
    CHECK(!greenhouse::toGreenhouseException(greenhouse::FAULT_NONE, &exception));

    // 缺水
    CHECK(greenhouse::hasWaterLackException(greenhouse::FAULT_WATER_LACK));
    CHECK(greenhouse::toGreenhouseException(greenhouse::FAULT_WATER_LACK, &exception));
    CHECK(exception == GreenhouseException::WATER_LACK_EXCEPTION);

    // 转盘
    CHECK(greenhouse::hasTurntableFailedException(greenhouse::FAULT_TURNTABLE_FAILED));
    CHECK(greenhouse::toGreenhouseException(greenhouse::FAULT_TURNTABLE_FAILED, &exception));
    CHECK(exception == GreenhouseException::TURNTABLE_FAILED_EXCEPTION);

    // 同时存在时缺水优先
    const uint32_t both = greenhouse::FAULT_WATER_LACK | greenhouse::FAULT_TURNTABLE_FAILED;
    CHECK(greenhouse::exceptionMaskFromFaults(both) ==
          (greenhouse::EXCEPTION_WATER_LACK | greenhouse::EXCEPTION_TURNTABLE_FAILED));
    CHECK(greenhouse::toGreenhouseException(both, &exception));
    CHECK(exception == GreenhouseException::WATER_LACK_EXCEPTION);

    // 与异常枚举无关的故障不产生异常
    CHECK(!greenhouse::toGreenhouseException(greenhouse::FAULT_OVER_TEMPERATURE, &exception));
}

} // namespace

int main()
{
    testPowerOnIsSafe();
    testHeaterHysteresis();
    testFanOnHighAirHumidity();
    testIrrigationDoseAndCooldown();
    testWaterLackStopsPump();
    testTemperatureSensorFault();
    testOverTemperatureInterlock();
    testLightOnlyInDaytime();
    testTurntableSwingAndPark();
    testManualModeTimeout();
    testRuntimeParamChange();
    testActuatorRejectionKeepsState();

    testOverTempSuppressesLight();
    testLowTempHighHumidityBlocksFan();
    testLowTempVeryHighHumidityPulsesFan();
    testOverTempBlocksPump();
    testPowerBudgetKeepsHeater();
    testLowHumidityIsDiagnosed();

    testOverTempBypassesFanGate();
    testPumpOffRetried();
    testPumpStopsWhenSoilWet();
    testLowHumidityHysteresis();
    testLightWithoutTimeBase();
    testTurntableBackoff();

    testVariableJudgeBasics();
    testVariableJudgeHysteresis();
    testVariableJudgeMaskAndOverride();

    testSnapshotToMonitoringData();
    testTelemetryReporterCadence();
    testTelemetryReporterActuatorChange();

    testExceptionMapping();

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return (0 == g_failures) ? 0 : 1;
}

#endif // LOGIC_HOST_TEST
