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
                                uint32_t bootNonce,
                                char *buffer,
                                std::size_t capacity)
{
    /* 与遥测同理：没有标识就不发，避免下游把它归进一个假的会话 */
    if (bootNonce == 0U)
    {
        return 0;
    }

    TextWriter writer(buffer, capacity);

    bool ok = writer.append("{\"v\":2,\"b\":\"")
              && writer.appendHex32(bootNonce)
              && writer.append("\",");

    ok = ok
         && writer.append("\"up\":")
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
