//
// Created by APairOfMoons on 2026/11/3.
//

#include "TelemetryPayload.h"

#include "TextWriter.h"


namespace {

/* 0.01 单位整数：四舍五入，NaN／无穷／越界一律回 0，避免未定义行为 */
int32_t toHundredths(double value)
{
    const double scaled = value * 100.0;

    if (!((scaled > -2147483649.0) && (scaled < 2147483648.0)))
    {
        return 0;
    }

    return static_cast<int32_t>((scaled >= 0.0) ? (scaled + 0.5) : (scaled - 0.5));
}

} // namespace


std::size_t TelemetryPayload::build(const MonitoringData &data,
                                    uint32_t sequence,
                                    uint32_t uptimeSeconds,
                                    char *buffer,
                                    std::size_t capacity)
{
    TextWriter writer(buffer, capacity);

    const bool ok = writer.append("{\"v\":1,\"seq\":")
                    && writer.appendUnsigned(sequence)
                    && writer.append(",\"up\":")
                    && writer.appendUnsigned(uptimeSeconds)
                    && writer.append(",\"t\":")
                    && writer.appendInt(toHundredths(data.temperature))
                    && writer.append(",\"ah\":")
                    && writer.appendInt(toHundredths(data.air_humidity))
                    && writer.append(",\"sh\":")
                    && writer.appendInt(toHundredths(data.soil_humidity))
                    && writer.append(",\"lux\":")
                    && writer.appendInt(data.illuminance)
                    && writer.append(",\"water\":")
                    && writer.appendBool(data.has_enough_water)
                    && writer.append(",\"fan\":")
                    && writer.appendBool(data.fan_on)
                    && writer.append(",\"light\":")
                    && writer.appendBool(data.light_on)
                    && writer.append(",\"heat\":")
                    && writer.appendBool(data.heater_on)
                    && writer.append('}');

    if (!ok || writer.isTruncated())
    {
        return 0;
    }

    return writer.length();
}
