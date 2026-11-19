//
// 温室控制单步编排实现。
//

#include "app/GreenhouseControlLoop.h"

#include "app/ExceptionMapping.h"

namespace greenhouse {
namespace {

constexpr ReporterConfig kDefaultReporterConfig = {
    30UL * 1000UL, // heartbeat_ms: 无变化时 30 s 定期上报
    1000UL,        // min_interval_ms: 最短 1 s 限流
    0.5F,          // temperature_delta: 0.5 ℃
    2.0F,          // air_humidity_delta: 2.0 %RH
    1.0F,          // soil_humidity_delta: 1.0 %
    50,            // illuminance_delta: 50 lx
    true,          // report_on_actuator_change: 执行器状态变化立即上报
};

struct ExceptionMapEntry
{
    uint32_t mask;
    GreenhouseException exception;
};

constexpr ExceptionMapEntry kExceptionMap[] = {
    {EXCEPTION_WATER_LACK, GreenhouseException::WATER_LACK_EXCEPTION},
    {EXCEPTION_TURNTABLE_FAILED, GreenhouseException::TURNTABLE_FAILED_EXCEPTION},
};

} // namespace

GreenhouseControlLoop::GreenhouseControlLoop(LogicController &controller,
                                             TelemetryReporter &reporter,
                                             IExceptionSink &exception_sink)
    : controller_(controller),
      reporter_(reporter),
      exception_sink_(exception_sink),
      last_exception_mask_(EXCEPTION_NONE),
      last_rising_mask_(EXCEPTION_NONE)
{
}

const ReporterConfig &GreenhouseControlLoop::defaultReporterConfig()
{
    return kDefaultReporterConfig;
}

void GreenhouseControlLoop::step(uint32_t now_ms)
{
    controller_.step(now_ms);
    const ControllerSnapshot &snap = controller_.snapshot();
    reporter_.update(now_ms, snap);
    dispatchExceptionEdges(snap.faults);
}

void GreenhouseControlLoop::resetExceptionState()
{
    last_exception_mask_ = EXCEPTION_NONE;
    last_rising_mask_ = EXCEPTION_NONE;
}

const ControllerSnapshot &GreenhouseControlLoop::snapshot() const
{
    return controller_.snapshot();
}

uint32_t GreenhouseControlLoop::lastExceptionMask() const
{
    return last_exception_mask_;
}

uint32_t GreenhouseControlLoop::lastRisingExceptionMask() const
{
    return last_rising_mask_;
}

void GreenhouseControlLoop::dispatchExceptionEdges(uint32_t faults)
{
    const uint32_t current_mask = exceptionMaskFromFaults(faults);
    const uint32_t rising = current_mask & (~last_exception_mask_);

    last_exception_mask_ = current_mask;
    last_rising_mask_ = rising;

    if (EXCEPTION_NONE == rising)
    {
        return;
    }

    for (const ExceptionMapEntry &entry : kExceptionMap)
    {
        if (0U != (rising & entry.mask))
        {
            exception_sink_.throwException(entry.exception);
        }
    }
}

} // namespace greenhouse
