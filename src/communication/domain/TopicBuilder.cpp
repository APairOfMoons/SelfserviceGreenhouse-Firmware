//
// Created by APairOfMoons on 2026/11/3.
//

#include "TopicBuilder.h"

#include <cstring>

#include "TextWriter.h"


const std::size_t TopicBuilder::MAX_DEVICE_ID_LENGTH;

bool TopicBuilder::build(char *buffer,
                         std::size_t capacity,
                         const char *deviceId,
                         const char *suffix)
{
    if ((deviceId == nullptr) || (suffix == nullptr))
    {
        return false;
    }

    const std::size_t deviceIdLength = std::strlen(deviceId);

    if ((deviceIdLength == 0) || (deviceIdLength > MAX_DEVICE_ID_LENGTH))
    {
        return false;
    }

    if (std::strchr(deviceId, '/') != nullptr)
    {
        return false;
    }

    TextWriter writer(buffer, capacity);

    const bool ok = writer.append("gh/")
                    && writer.append(deviceId)
                    && writer.append('/')
                    && writer.append(suffix);

    return ok && !writer.isTruncated();
}

bool TopicBuilder::telemetryTopic(char *buffer, std::size_t capacity, const char *deviceId)
{
    return build(buffer, capacity, deviceId, "telemetry");
}

bool TopicBuilder::alarmTopic(char *buffer,
                              std::size_t capacity,
                              const char *deviceId,
                              const char *code)
{
    if ((code == nullptr) || (code[0] == '\0'))
    {
        return false;
    }

    /* 复用 build 的 deviceId 校验：先拼 "alarm/"，再交给 build 拼前缀 */
    char suffix[32];
    TextWriter suffixWriter(suffix, sizeof(suffix));

    if (!suffixWriter.append("alarm/") || !suffixWriter.append(code))
    {
        return false;
    }

    return build(buffer, capacity, deviceId, suffix);
}

bool TopicBuilder::statusTopic(char *buffer, std::size_t capacity, const char *deviceId)
{
    return build(buffer, capacity, deviceId, "status");
}

bool TopicBuilder::commandTopic(char *buffer, std::size_t capacity, const char *deviceId)
{
    return build(buffer, capacity, deviceId, "cmd");
}
