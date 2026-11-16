//
// Created by badk on 2026/11/4.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_COMMCONFIGURATION_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_COMMCONFIGURATION_H

#include <cstdint>

/**
 * 通信配置（本模块独占）。
 *
 * 项目不公开，凭据直接硬编码在这里；换设备/换服务器时改这一份即可。
 * DEVICE_ID 同时用作 MQTT 的 username 与 clientId，必须全局唯一。
 */
struct CommStatics
{
    const char *DEVICE_ID;

    const char *WIFI_SSID;
    const char *WIFI_PASSWORD;

    const char *BROKER_HOST;
    const int BROKER_PORT;
    const char *MQTT_PASSWORD;

    const uint32_t RECONNECT_MIN_MS;
    const uint32_t RECONNECT_MAX_MS;
    const uint32_t AT_TIMEOUT_MS;
};

constexpr CommStatics COMM_CONFIGURATION = {
    "GH-0001",

    "LAPTOP-KC3EICKH 0359",
    "5qD9]816",

    "192.168.137.1",
    1883,
    "Mq#Password",

    2000U,
    60000U,
    2000U,
};

#endif //SELFSERVICEGREENHOUSE_FIRMWARE_COMMCONFIGURATION_H
