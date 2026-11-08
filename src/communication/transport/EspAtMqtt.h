//
// Created by APairOfMoons on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ESPATMQTT_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ESPATMQTT_H

#include <cstdint>


/**
 * MQTT 相关的 AT 命令。
 *
 * 与 EspAtWifi 一样只做序列封装、不留状态。字符串参数里的双引号会转义成
 * `\"`，否则 LWT 报文那种含 JSON 的字段会把命令行截断。
 *
 * 注意：`+MQTTPUB:OK` 只表示模块协议栈收下了这条报文，不代表 broker 确认
 * （约束 C6）。可靠性靠 QoS1 和客户端侧的等待。
 */
class EspAtMqtt
{
    public:
    /**
     * AT+MQTTUSERCFG + AT+MQTTCONNCFG。
     *
     * @param clientId   同时用作 MQTT clientId
     * @param username   同时用作 MQTT username（设备 ID）
     * @param password   MQTT 密码
     * @param lwtTopic   LWT topic，通常就是 status
     * @param lwtPayload LWT 报文，含双引号也没问题，这里会转义
     */
    static bool configure(const char *clientId,
                          const char *username,
                          const char *password,
                          const char *lwtTopic,
                          const char *lwtPayload,
                          uint32_t timeoutMs);

    /**
     * AT+MQTTCONN，并且等到 +MQTTCONNECTED。
     *
     * OK 只表示命令被接受，真正连上与否看 URC（约束 C5），所以这里会
     * 继续等事件；正常情况下 URC 先于 OK 到达，已经在槽里了。
     */
    static bool connect(const char *host, uint16_t port, uint32_t timeoutMs);

    /** AT+MQTTCLEAN，清掉模块里保存的会话。 */
    static bool cleanSession(uint32_t timeoutMs);

    /** AT+MQTTSUB，重复订阅时把 ALREADY SUBSCRIBE 也算成功。 */
    static bool subscribe(const char *topic, uint32_t timeoutMs);

    /**
     * AT+MQTTPUBRAW 发一条报文。
     *
     * 用 RAW 版本是因为普通 PUB 要求 data 用双引号包裹，JSON 里的引号
     * 会破坏命令解析（约束 C2）。
     */
    static bool publish(const char *topic,
                        const uint8_t *payload,
                        uint32_t length,
                        uint8_t qos,
                        bool retain,
                        uint32_t timeoutMs);
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_ESPATMQTT_H
