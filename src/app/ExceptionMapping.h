//
// 故障位到 communication/GreenhouseException.h 枚举的映射。
// 只做转换，不直接调用 MessageSender，避免逻辑层依赖上报通道。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_EXCEPTIONMAPPING_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_EXCEPTIONMAPPING_H

#include <stdint.h>

#include "communication/GreenhouseException.h"
#include "logic/LogicController.h"

namespace greenhouse {

// 与 GreenhouseException 一一对应的位掩码（可同时置位）。
enum ExceptionMask : uint32_t
{
    EXCEPTION_NONE = 0U,
    EXCEPTION_WATER_LACK = 1U << 0,         // WATER_LACK_EXCEPTION
    EXCEPTION_TURNTABLE_FAILED = 1U << 1,   // TURNTABLE_FAILED_EXCEPTION
};

// 从 LogicController 的故障位提取异常掩码。
uint32_t exceptionMaskFromFaults(uint32_t faults);

bool hasWaterLackException(uint32_t faults);
bool hasTurntableFailedException(uint32_t faults);

// 有对应异常时写 *out_exception 并返回 true；缺水优先于转盘故障。
bool toGreenhouseException(uint32_t faults, GreenhouseException *out_exception);

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_EXCEPTIONMAPPING_H
