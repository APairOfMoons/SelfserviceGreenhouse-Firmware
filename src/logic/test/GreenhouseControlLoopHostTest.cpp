//
// GreenhouseControlLoop PC 端宿主测试（不进固件）。
//
// 用法（在仓库根目录，主机编译器，C++11）：
//   g++ -std=c++11 -DLOGIC_HOST_TEST -Isrc src/logic/LogicController.cpp
//       src/logic/TelemetryReporter.cpp
//       src/logic/FakeHardware.cpp src/app/ExceptionMapping.cpp
//       src/app/MonitoringDataMapper.cpp src/app/GreenhouseControlLoop.cpp
//       src/configuration/LogicalConfiguration.cpp
//       src/logic/test/GreenhouseControlLoopHostTest.cpp -o loop_test.exe
//   .\loop_test.exe
//
// 未定义 LOGIC_HOST_TEST 时本文件编译为空，不影响固件构建。
//

#if defined(LOGIC_HOST_TEST)

#include <cstdio>

#include "app/ExceptionMapping.h"
#include "app/GreenhouseControlLoop.h"
#include "configuration/LogicalConfiguration.h"
#include "logic/FakeHardware.h"
#include "logic/ITelemetrySink.h"
#include "logic/LogicController.h"
#include "logic/TelemetryReporter.h"

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

bool nearly(double a, double b, double eps)
{
    const double delta = (a > b) ? (a - b) : (b - a);
    return delta <= eps;
}

void initTestParams(LogicalStatics *out_params)
{
    LogicalStatics params = {};

    params.variable.turnAnglePerDay = 120;

    params.variable.minTemperature = 18.0;
    params.variable.maxTemperature = 28.0;
    params.variable.temperatureHysteresis = 1.5;

    params.variable.minAirHumidity = 30.0;
    params.variable.maxAirHumidity = 75.0;
    params.variable.airHumidityHysteresis = 5.0;

    params.variable.minSoilHumidity = 35.0;
    params.variable.maxSoilHumidity = 55.0;

    params.variable.minIlluminance = 1200.0;
    params.variable.maxIlluminance = 100000.0;
    params.variable.illuminanceMinOnMs = 10UL * kMinute;

    params.variable.dayStartMs = 7UL * kHour;
    params.variable.dayLengthMs = 10UL * kHour;

    params.variable.pumpDurationMs = 800UL;
    params.variable.pumpMinIntervalMs = 6UL * kHour;

    params.variable.maxTurnAngle = 60;
    params.variable.turnStepAngle = 15;
    params.variable.turnIntervalMs = 60UL * kSecond;

    params.variable.samplePeriodMs = kStepMs;

    params.fixed.sensorFailLimit = 3U;
    params.fixed.manualTimeoutMs = 60UL * kSecond;

    *out_params = params;
}

class FakeTelemetrySink : public greenhouse::ITelemetrySink
{
    public:
    bool accept;
    uint32_t calls;
    MonitoringData last;

    FakeTelemetrySink() : accept(true), calls(0UL), last() {}

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

class FakeExceptionSink : public greenhouse::IExceptionSink
{
    public:
    uint32_t total_calls;
    uint32_t water_lack_calls;
    uint32_t turntable_failed_calls;

    FakeExceptionSink()
        : total_calls(0UL),
          water_lack_calls(0UL),
          turntable_failed_calls(0UL)
    {
    }

    void throwException(GreenhouseException exception) override
    {
        total_calls++;
        if (GreenhouseException::WATER_LACK_EXCEPTION == exception)
        {
            water_lack_calls++;
        }
        else if (GreenhouseException::TURNTABLE_FAILED_EXCEPTION == exception)
        {
            turntable_failed_calls++;
        }
    }
};

struct LoopHarness
{
    greenhouse::FakeHardware hw;
    LogicalStatics params;
    greenhouse::LogicController ctrl;
    FakeTelemetrySink telemetry_sink;
    greenhouse::TelemetryReporter reporter;
    FakeExceptionSink exception_sink;
    greenhouse::GreenhouseControlLoop loop;
    uint32_t now_ms;

    LoopHarness()
        : hw(),
          params(),
          ctrl(hw, (initTestParams(&params), params)),
          telemetry_sink(),
          reporter(telemetry_sink, greenhouse::GreenhouseControlLoop::defaultReporterConfig()),
          exception_sink(),
          loop(ctrl, reporter, exception_sink),
          now_ms(1000UL)
    {
    }

    void stepOnce()
    {
        loop.step(now_ms);
        now_ms += kStepMs;
    }

    void advance(uint32_t duration_ms)
    {
        const uint32_t end_ms = now_ms + duration_ms;
        while (now_ms < end_ms)
        {
            stepOnce();
        }
    }
};

void testStepLinksControlAndTelemetry()
{
    LoopHarness h;
    h.hw.temperature = 15.0F; // 低于 18 ℃，首拍触发加热
    h.hw.air_humidity = 50.0F;
    h.hw.soil_humidity = 45.0F;
    h.hw.illuminance = 5000U;
    h.hw.has_enough_water = true;

    h.stepOnce();

    CHECK(h.reporter.hasPublished());
    CHECK(h.telemetry_sink.calls == 1UL);
    CHECK(nearly(h.telemetry_sink.last.temperature, 15.0, 0.001));
    CHECK(h.telemetry_sink.last.heater_on);
    CHECK(h.telemetry_sink.last.has_enough_water);
    CHECK(h.exception_sink.total_calls == 0UL);

    // 1 s 限流期内无变化不重复上报
    h.advance(500UL);
    CHECK(h.telemetry_sink.calls == 1UL);

    // 超过 min_interval_ms 且温度显著变化 -> 立即上报
    h.hw.temperature = 25.0F;
    h.advance(1000UL);
    CHECK(h.telemetry_sink.calls == 2UL);
    CHECK(nearly(h.telemetry_sink.last.temperature, 25.0, 0.001));

    // 无变化按 30 s 心跳上报
    const uint32_t before_heartbeat = h.telemetry_sink.calls;
    h.advance(31UL * kSecond);
    CHECK(h.telemetry_sink.calls == before_heartbeat + 1UL);
}

void testWaterLackRisingEdgeNoSpamAndRetrigger()
{
    LoopHarness h;
    h.hw.has_enough_water = false;

    // 首次采样即缺水：0 -> 1 上升沿仅报一次
    h.stepOnce();
    CHECK(h.exception_sink.water_lack_calls == 1UL);
    CHECK(h.exception_sink.total_calls == 1UL);
    CHECK(h.loop.lastExceptionMask() == greenhouse::EXCEPTION_WATER_LACK);
    CHECK(h.loop.lastRisingExceptionMask() == greenhouse::EXCEPTION_WATER_LACK);

    // 持续缺水多拍不刷屏
    h.advance(2000UL);
    CHECK(h.exception_sink.water_lack_calls == 1UL);
    CHECK(h.loop.lastRisingExceptionMask() == greenhouse::EXCEPTION_NONE);

    // 补水恢复
    h.hw.has_enough_water = true;
    h.advance(500UL);
    CHECK(h.loop.lastExceptionMask() == greenhouse::EXCEPTION_NONE);
    CHECK(h.exception_sink.water_lack_calls == 1UL);

    // 再次缺水 -> 新的 0 -> 1 跳变再次上报一次
    h.hw.has_enough_water = false;
    h.advance(500UL);
    CHECK(h.exception_sink.water_lack_calls == 2UL);
    CHECK(h.exception_sink.total_calls == 2UL);
}

void testTurntableFailureRisingEdgeAndNonExceptionFaultsIgnored()
{
    LoopHarness h;
    h.ctrl.setTimeOfDay(12UL * kHour); // 白天窗口内允许转动

    // 非异常故障（超温、温度传感器失败）不应触发 throwException
    h.hw.temperature = 40.0F;
    h.stepOnce();
    CHECK((h.loop.snapshot().faults & greenhouse::FAULT_OVER_TEMPERATURE) != 0U);
    CHECK(h.exception_sink.total_calls == 0UL);

    h.hw.temperature = 24.0F;
    h.hw.fail_temperature = true;
    h.advance(500UL);
    CHECK((h.loop.snapshot().faults & greenhouse::FAULT_TEMPERATURE_SENSOR) != 0U);
    CHECK(h.exception_sink.total_calls == 0UL);
    h.hw.fail_temperature = false;

    // 注入转盘连续失败（kTurnFailLimit = 5, 每次失败后退避 60 s）
    LogicalStatics fast_turn = h.params;
    fast_turn.variable.turnAnglePerDay = 0;
    fast_turn.variable.turnIntervalMs = 1000UL;
    CHECK(LogicalConfiguration::modifyConfiguration(fast_turn.variable));
    h.ctrl.setConfiguration(fast_turn);

    h.hw.fail_turntable = true;
    h.advance(250UL * kSecond);
    CHECK((h.loop.snapshot().faults & greenhouse::FAULT_TURNTABLE_FAILED) != 0U);
    CHECK(h.exception_sink.turntable_failed_calls == 1UL);
    CHECK(h.exception_sink.water_lack_calls == 0UL);

    // 持续故障不刷屏
    h.advance(120UL * kSecond);
    CHECK(h.exception_sink.turntable_failed_calls == 1UL);

    // 恢复成功后再清故障
    h.hw.fail_turntable = false;
    h.advance(65UL * kSecond);
    CHECK((h.loop.snapshot().faults & greenhouse::FAULT_TURNTABLE_FAILED) == 0U);
    CHECK(h.loop.lastExceptionMask() == greenhouse::EXCEPTION_NONE);
}

void testSimultaneousRisingEdgesInSameStepAndReset()
{
    LoopHarness h;
    h.ctrl.setTimeOfDay(12UL * kHour);

    LogicalStatics fast_turn = h.params;
    fast_turn.variable.turnAnglePerDay = 0;
    fast_turn.variable.turnIntervalMs = 1000UL;
    CHECK(LogicalConfiguration::modifyConfiguration(fast_turn.variable));
    h.ctrl.setConfiguration(fast_turn);

    // 第 1 拍 t=1000 记录 last_turn_ms_=1000；此后每 1000ms + 60s 失败一次：
    // 第1次 t=2000，第2次 t=62000，第3次 t=122000，第4次 t=182000，第5次 t=242000。
    h.hw.has_enough_water = true;
    h.hw.fail_turntable = true;
    while (h.now_ms < 242000UL)
    {
        h.stepOnce();
    }
    CHECK(h.hw.turntable_attempts == 4UL);
    CHECK(h.exception_sink.total_calls == 0UL);

    // 在第 5 次转盘失败触发的同一拍（t=242000）同时缺水，验证同拍双上升沿都报出
    h.hw.has_enough_water = false;
    h.stepOnce();
    CHECK(h.hw.turntable_attempts == 5UL);
    CHECK(h.loop.lastRisingExceptionMask() ==
          (greenhouse::EXCEPTION_WATER_LACK | greenhouse::EXCEPTION_TURNTABLE_FAILED));
    CHECK(h.exception_sink.water_lack_calls == 1UL);
    CHECK(h.exception_sink.turntable_failed_calls == 1UL);
    CHECK(h.exception_sink.total_calls == 2UL);
    CHECK(h.loop.lastExceptionMask() ==
          (greenhouse::EXCEPTION_WATER_LACK | greenhouse::EXCEPTION_TURNTABLE_FAILED));

    // 多拍保持，不刷屏
    h.advance(1000UL);
    CHECK(h.exception_sink.total_calls == 2UL);

    // resetExceptionState 后，若故障仍存在则下一拍会再次上报
    h.loop.resetExceptionState();
    h.stepOnce();
    CHECK(h.exception_sink.water_lack_calls == 2UL);
    CHECK(h.exception_sink.turntable_failed_calls == 2UL);
}

void testConfigurationHotReloadDuringLoop()
{
    LogicalStatics saved = {};
    LogicalConfiguration::snapshot(&saved);

    LoopHarness h;
    h.hw.temperature = 21.0F;
    h.stepOnce();
    CHECK(!h.loop.snapshot().heater_on);

    // 通过 LogicalConfiguration 热更新提高 minTemperature，下一拍自动生效并同步上报
    LogicalStatics updated = saved;
    updated.variable.minTemperature = 23.0;
    updated.variable.maxTemperature = 30.0;
    updated.variable.temperatureHysteresis = 1.5;
    CHECK(LogicalConfiguration::modifyConfiguration(updated.variable));

    h.advance(1100UL);
    CHECK(h.loop.snapshot().heater_on);
    CHECK(h.telemetry_sink.last.heater_on);

    CHECK(LogicalConfiguration::modifyConfiguration(saved.variable));
}

} // namespace

int main()
{
    testStepLinksControlAndTelemetry();
    testWaterLackRisingEdgeNoSpamAndRetrigger();
    testTurntableFailureRisingEdgeAndNonExceptionFaultsIgnored();
    testSimultaneousRisingEdgesInSameStepAndReset();
    testConfigurationHotReloadDuringLoop();

    std::printf("%d/%d checks passed\n", g_checks - g_failures, g_checks);
    return (0 == g_failures) ? 0 : 1;
}

#endif // LOGIC_HOST_TEST
