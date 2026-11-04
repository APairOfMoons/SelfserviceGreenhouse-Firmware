//
// Created by APairOfMoons on 2026/11/3.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_TOPICBUILDER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_TOPICBUILDER_H

#include <cstddef>


/**
 * topic 拼装。
 *
 * 统一格式：gh/<deviceId>/<suffix>。deviceId 同时是 MQTT 用户名，
 * 因此这里拒绝空串、超长、以及包含 '/' 的值（否则会拼出多级 topic，
 * 绕过 ACL 的设备隔离）。
 */
class TopicBuilder
{
    public:
    static const std::size_t MAX_DEVICE_ID_LENGTH = 32;

    static bool build(char *buffer,
                      std::size_t capacity,
                      const char *deviceId,
                      const char *suffix);

    static bool telemetryTopic(char *buffer, std::size_t capacity, const char *deviceId);
    static bool alarmTopic(char *buffer, std::size_t capacity, const char *deviceId, const char *code);
    static bool statusTopic(char *buffer, std::size_t capacity, const char *deviceId);
    static bool configTopic(char *buffer, std::size_t capacity, const char *deviceId);
    static bool configAckTopic(char *buffer, std::size_t capacity, const char *deviceId);
    static bool commandTopic(char *buffer, std::size_t capacity, const char *deviceId);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_TOPICBUILDER_H
