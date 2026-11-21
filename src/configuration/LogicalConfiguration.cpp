//
// 逻辑配置实现：默认值、物理护栏、校验、提交。
//

#include "configuration/LogicalConfiguration.h"

#include "configuration/CriticalSection.h"

#if defined(LOGIC_HOST_TEST)
#define GREENHOUSE_ENTER_CRITICAL() ((void) 0)
#define GREENHOUSE_EXIT_CRITICAL()  ((void) 0)
#else
#define GREENHOUSE_ENTER_CRITICAL() greenhouseEnterCritical()
#define GREENHOUSE_EXIT_CRITICAL()  greenhouseExitCritical()
#endif

namespace
{

constexpr uint32_t kMinuteMs = 60UL * 1000UL;
constexpr uint32_t kHourMs = 60UL * kMinuteMs;
constexpr uint32_t kDayMs = 24UL * kHourMs;

// 物理护栏：与具体植物无关，超出即判为数据错误（配置模块内部常量）。
constexpr double kPhysicalMinTemperature = -10.0;
constexpr double kPhysicalMaxTemperature = 50.0;
constexpr double kPhysicalMinHumidity = 0.0;
constexpr double kPhysicalMaxHumidity = 100.0;
constexpr double kPhysicalMinIlluminance = 0.0;
constexpr double kPhysicalMaxIlluminance = 200000.0;

bool isFinite(double value)
{
    return (value == value) && (value <= 1.0e30) && (value >= -1.0e30);
}

bool inLimits(double value, double low, double high)
{
    return (value >= low) && (value <= high);
}

bool validate(const VariableStatics &candidate)
{
    // 1) 有限性：报文解析出错时可能是 NaN / Inf
    const bool finite =
        isFinite(candidate.minTemperature) && isFinite(candidate.maxTemperature) &&
        isFinite(candidate.temperatureHysteresis) && isFinite(candidate.minAirHumidity) &&
        isFinite(candidate.maxAirHumidity) && isFinite(candidate.airHumidityHysteresis) &&
        isFinite(candidate.minSoilHumidity) && isFinite(candidate.maxSoilHumidity) &&
        isFinite(candidate.minIlluminance) && isFinite(candidate.maxIlluminance);

    if (!finite)
    {
        return false;
    }

    // 2) 物理护栏
    const bool withinLimits =
        inLimits(candidate.minTemperature, kPhysicalMinTemperature, kPhysicalMaxTemperature) &&
        inLimits(candidate.maxTemperature, kPhysicalMinTemperature, kPhysicalMaxTemperature) &&
        inLimits(candidate.minAirHumidity, kPhysicalMinHumidity, kPhysicalMaxHumidity) &&
        inLimits(candidate.maxAirHumidity, kPhysicalMinHumidity, kPhysicalMaxHumidity) &&
        inLimits(candidate.minSoilHumidity, kPhysicalMinHumidity, kPhysicalMaxHumidity) &&
        inLimits(candidate.maxSoilHumidity, kPhysicalMinHumidity, kPhysicalMaxHumidity) &&
        inLimits(candidate.minIlluminance, kPhysicalMinIlluminance, kPhysicalMaxIlluminance) &&
        inLimits(candidate.maxIlluminance, kPhysicalMinIlluminance, kPhysicalMaxIlluminance);

    if (!withinLimits)
    {
        return false;
    }

    // 3) 自洽性
    const bool ordered = (candidate.minTemperature < candidate.maxTemperature) &&
                         (candidate.minAirHumidity < candidate.maxAirHumidity) &&
                         (candidate.minSoilHumidity < candidate.maxSoilHumidity) &&
                         (candidate.minIlluminance < candidate.maxIlluminance);

    const bool hysteresisOk =
        (candidate.temperatureHysteresis > 0.0) &&
        (candidate.temperatureHysteresis < (candidate.maxTemperature - candidate.minTemperature)) &&
        (candidate.airHumidityHysteresis > 0.0) &&
        (candidate.airHumidityHysteresis < (candidate.maxAirHumidity - candidate.minAirHumidity));

    const bool pumpOk = (candidate.pumpDurationMs > 0UL) &&
                        (candidate.pumpDurationMs <= 10UL * kMinuteMs) &&
                        (candidate.pumpMinIntervalMs >= candidate.pumpDurationMs);

    const bool lightOk = (candidate.illuminanceMinOnMs <= kDayMs) &&
                         (candidate.illuminanceHysteresis >= 0.0) &&
                         (candidate.illuminanceHysteresis <
                          (candidate.maxIlluminance - candidate.minIlluminance)) &&
                         (candidate.dayLengthMs >= kMinuteMs) &&
                         (candidate.dayLengthMs <= kDayMs) && (candidate.dayStartMs < kDayMs);

    const bool turnOk = (candidate.maxTurnAngle >= 0) && (candidate.maxTurnAngle <= 180) &&
                        (candidate.turnStepAngle > 0) &&
                        (candidate.turnStepAngle <= candidate.maxTurnAngle) &&
                        (candidate.turnIntervalMs >= 1000UL);

    // 每日累计受光角度：用来换算转动次数与间隔，0 表示"不按每日角度调度"。
    const bool turnAngleOk =
        (candidate.turnAnglePerDay >= 0) && (candidate.turnAnglePerDay <= 3600);

    const bool sampleOk = (candidate.samplePeriodMs >= 10UL) &&
                          (candidate.samplePeriodMs <= kMinuteMs);

    return ordered && hysteresisOk && pumpOk && lightOk && turnOk && turnAngleOk &&
           sampleOk;
}

LogicalStatics makeDefaultConfiguration()
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
    config.variable.illuminanceHysteresis = 200.0;
    config.variable.illuminanceMinOnMs = 10UL * kMinuteMs;

    config.variable.dayStartMs = 7UL * kHourMs;
    config.variable.dayLengthMs = 10UL * kHourMs;

    config.variable.pumpDurationMs = 800UL;
    config.variable.pumpMinIntervalMs = 6UL * kHourMs;

    config.variable.maxTurnAngle = 60;
    // 实际养护每天只转 10~30°，步角取 10° 可整除整段区间（10→1 次、30→3 次）；
    // 电机是 4096 步/圈，10° 完全转得出来，这里只是软件调度粒度。
    config.variable.turnStepAngle = 10;
    config.variable.turnIntervalMs = 20UL * kMinuteMs;

    config.variable.samplePeriodMs = 1000UL;

    config.fixed.sensorFailLimit = 3U;
    config.fixed.manualTimeoutMs = 30UL * kMinuteMs;

    return config;
}

} // namespace

LogicalStatics LOGICAL_CONFIGURATION = makeDefaultConfiguration();
static uint32_t g_configurationVersion = 0UL;

bool LogicalConfiguration::modifyConfiguration(const VariableStatics &candidate)
{
    if (!validate(candidate))
    {
        return false; // 整份拒绝，当前配置保持不变
    }

    GREENHOUSE_ENTER_CRITICAL();
    LOGICAL_CONFIGURATION.variable = candidate;
    g_configurationVersion++;
    GREENHOUSE_EXIT_CRITICAL();

    return true;
}

void LogicalConfiguration::snapshot(LogicalStatics *out)
{
    if (nullptr == out)
    {
        return;
    }

    GREENHOUSE_ENTER_CRITICAL();
    *out = LOGICAL_CONFIGURATION;
    GREENHOUSE_EXIT_CRITICAL();
}
uint32_t LogicalConfiguration::version()
{
    uint32_t value = 0UL;
    GREENHOUSE_ENTER_CRITICAL();
    value = g_configurationVersion;
    GREENHOUSE_EXIT_CRITICAL();
    return value;
}