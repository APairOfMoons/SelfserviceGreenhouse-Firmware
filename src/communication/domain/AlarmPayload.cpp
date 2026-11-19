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

        case GreenhouseException::TEMPERATURE_SENSOR_EXCEPTION:
            return "temp_sensor";

        case GreenhouseException::AIR_HUMIDITY_SENSOR_EXCEPTION:
            return "humidity_sensor";

        case GreenhouseException::SOIL_HUMIDITY_SENSOR_EXCEPTION:
            return "soil_sensor";

        case GreenhouseException::ILLUMINANCE_SENSOR_EXCEPTION:
            return "lux_sensor";

        case GreenhouseException::OVER_TEMPERATURE_EXCEPTION:
            return "over_temp";

        case GreenhouseException::PUMP_TIMEOUT_EXCEPTION:
            return "pump_timeout";

        case GreenhouseException::TURNTABLE_FAILED_EXCEPTION:
            return "turntable_failed";

        case GreenhouseException::TIME_UNSET_EXCEPTION:
            return "time_unset";

        case GreenhouseException::AIR_HUMIDITY_LOW_EXCEPTION:
            return "humidity_low";

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
