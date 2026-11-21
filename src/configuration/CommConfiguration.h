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

    /*
     * 本地时区相对 UTC 的分钟偏移，东八区 = 480。
     *
     * SNTP 固定按 UTC 取（模块返回的字符串里没有时区字段，见
     * communication/docs/AT_COMMAND_SET.md §4.6），"本地时间"是固件自己加出来的。
     * 换部署地只改这一个值；现行规则不处理夏令时。
     */
    const int TIMEZONE_OFFSET_MINUTES;

    const uint32_t RECONNECT_MIN_MS;
    const uint32_t RECONNECT_MAX_MS;
    const uint32_t AT_TIMEOUT_MS;
};

constexpr CommStatics COMM_CONFIGURATION = {
    "GH-0001",

    "LAPTOP-KC3EICKH",
    "5qD9]816",

    "endpoint.blackblade.dev",
    1883,
    "Mq#Password",

    480,

    2000U,
    60000U,
    2000U,
};

#endif //SELFSERVICEGREENHOUSE_FIRMWARE_COMMCONFIGURATION_H
