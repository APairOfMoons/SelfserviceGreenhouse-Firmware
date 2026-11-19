//
// 温室控制单步编排：无 RTOS / FSP 依赖，串联控制、遥测上报与异常上升沿分发。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_GREENHOUSECONTROLLOOP_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_GREENHOUSECONTROLLOOP_H

#include <stdint.h>

#include "communication/GreenhouseException.h"
#include "logic/LogicController.h"
#include "logic/TelemetryReporter.h"

namespace greenhouse {

class IExceptionSink
{
    public:
    virtual ~IExceptionSink() = default;

    virtual void throwException(GreenhouseException exception) = 0;
};

class GreenhouseControlLoop
{
    public:
    GreenhouseControlLoop(LogicController &controller,
                          TelemetryReporter &reporter,
                          IExceptionSink &exception_sink);

    // 默认遥测上报策略（返回静态常驻配置引用，避免结构体值返回）。
    static const ReporterConfig &defaultReporterConfig();

    // 单步执行：controller.step -> reporter.update -> 异常上升沿上报。
    void step(uint32_t now_ms);

    // 清空异常上升沿记忆（例如链路恢复或重新开始监控时）。
    void resetExceptionState();

    const ControllerSnapshot &snapshot() const;
    uint32_t lastExceptionMask() const;
    uint32_t lastRisingExceptionMask() const;

    private:
    void dispatchExceptionEdges(uint32_t faults);

    LogicController &controller_;
    TelemetryReporter &reporter_;
    IExceptionSink &exception_sink_;
    uint32_t last_exception_mask_;
    uint32_t last_rising_mask_;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_GREENHOUSECONTROLLOOP_H
