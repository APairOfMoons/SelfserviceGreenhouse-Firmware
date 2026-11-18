//
// Created by APairOfMoons on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ESPATMQTT_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ESPATMQTT_H

#include <cstdint>


/** 最近一次 MQTT 交互失败在哪一步。0 表示这一轮还没失败过。 */
enum class MqttFailStep : uint8_t
{
    kNone = 0,
    kParam = 1,     ///< 参数本身不合法（含引号/反斜杠），命令根本没发出去
    kUserCfg = 2,   ///< AT+MQTTUSERCFG
    kConnCfg = 3,   ///< AT+MQTTCONNCFG
    kConnect = 4,   ///< AT+MQTTCONN
    kSubscribe = 5, ///< AT+MQTTSUB
    kPublish = 6,   ///< AT+MQTTPUBRAW
};


/*
 * 失败现场，供调试器查看（与 AtChannel / CommunicationService 同一套路）。
 *
 * g_atLastLine 也会留一行，但失败之后状态机通常还会补发 AT+MQTTCLEAN 之类，
 * 那一行很快被覆盖；这里在失败发生的当口单独存一份，才留得住。
 */
extern "C"
{
extern volatile uint8_t g_mqttFailStep;   /* MqttFailStep */
extern volatile uint8_t g_mqttFailStatus; /* AtStatus */
extern char g_mqttFailLine[];
}


/**
 * MQTT 相关的 AT 命令。
 *
 * 与 EspAtWifi 一样只做序列封装、不留状态。字符串参数按原样放进双引号里，
 * **不做转义**：这块固件不支持 `\"`，实测含转义引号的 CONNCFG 直接回 ERROR
 * （见 docs/AT_COMMAND_SET.md §4.1）。参数里一旦出现引号或反斜杠，整条命令
 * 当场判失败，不发给模块——报一个明确的失败，好过发一条注定被拒的命令。
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
     * CONNCFG 失败**不算整体失败**：它只登记遗嘱与 keepalive，缺了还能连上、
     * 还能上报，只是设备失联时客户端看不到 offline。USERCFG 失败则必然连不上，
     * 直接返回 false。
     *
     * @param clientId   同时用作 MQTT clientId
     * @param username   同时用作 MQTT username（设备 ID）
     * @param password   MQTT 密码
     * @param lwtTopic   LWT topic，通常就是 status
     * @param lwtPayload LWT 报文，只能是纯文本，不能含双引号或反斜杠
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
