//
// 故障位与异常枚举的映射实现。
//

#include "app/ExceptionMapping.h"

namespace greenhouse {

uint32_t exceptionMaskFromFaults(uint32_t faults)
{
    uint32_t mask = EXCEPTION_NONE;

    if (0U != (faults & FAULT_WATER_LACK))
    {
        mask |= EXCEPTION_WATER_LACK;
    }
    if (0U != (faults & FAULT_TURNTABLE_FAILED))
    {
        mask |= EXCEPTION_TURNTABLE_FAILED;
    }

    return mask;
}

bool hasWaterLackException(uint32_t faults)
{
    return 0U != (exceptionMaskFromFaults(faults) & EXCEPTION_WATER_LACK);
}

bool hasTurntableFailedException(uint32_t faults)
{
    return 0U != (exceptionMaskFromFaults(faults) & EXCEPTION_TURNTABLE_FAILED);
}

bool toGreenhouseException(uint32_t faults, GreenhouseException *out_exception)
{
    if (nullptr == out_exception)
    {
        return false;
    }

    if (hasWaterLackException(faults))
    {
        *out_exception = GreenhouseException::WATER_LACK_EXCEPTION;
        return true;
    }

    if (hasTurntableFailedException(faults))
    {
        *out_exception = GreenhouseException::TURNTABLE_FAILED_EXCEPTION;
        return true;
    }

    return false;
}

} // namespace greenhouse
