//
// Created by APairOfMoons on 2026/11/3.
//

#include "AlarmPayload.h"

#include "TextWriter.h"


const char *AlarmPayload::code(GreenhouseException exception)
{
    switch (exception)
    {
        case GreenhouseException::WATER_LACK_EXCEPTION:
            return "water_lack";

        case GreenhouseException::TURNTABLE_FAILED_EXCEPTION:
            return "turntable_failed";

        default:
            return "unknown";
    }
}

std::size_t AlarmPayload::build(GreenhouseException exception,
                                uint32_t uptimeSeconds,
                                char *buffer,
                                std::size_t capacity)
{
    TextWriter writer(buffer, capacity);

    const bool ok = writer.append("{\"v\":1,\"up\":")
                    && writer.appendUnsigned(uptimeSeconds)
                    && writer.append(",\"code\":\"")
                    && writer.append(code(exception))
                    && writer.append("\",\"active\":true}");

    if (!ok || writer.isTruncated())
    {
        return 0;
    }

    return writer.length();
}
