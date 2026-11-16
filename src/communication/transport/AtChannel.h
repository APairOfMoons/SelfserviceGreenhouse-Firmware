//
// Created by APairOfMoons on 2026/11/8.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ATCHANNEL_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ATCHANNEL_H

#include <cstdint>

/* 一条 AT 响应行的最大长度。最长的已知行是 +MQTTSUBRECV 的前导，约 90 字节。 */
constexpr uint32_t AT_LINE_MAX = 128U;

/* MQTT topic 的最大长度，与 TopicBuilder 的上限一致。 */
constexpr uint32_t AT_TOPIC_MAX = 64U;

/* 单条下行配置报文的接收上限。超出即截断，由上层按语法错误拒绝。 */
constexpr uint32_t AT_PAYLOAD_MAX = 224U;

/* 单条 AT 命令的最大长度。ESP8266 的 AT 固件上限是 256 字节（约束 C2）。 */
constexpr uint32_t AT_COMMAND_MAX = 256U;


/** AT 命令的终结状态。 */
enum class AtStatus : uint8_t
{
    kOk,        ///< 收到 OK
    kError,     ///< 收到 ERROR
    kFail,      ///< 收到 FAIL（AT+CWJAP 一类的失败返回，与 ERROR 不同）
    kPrompt,    ///< 收到 '>'，可以发裸数据了
    kTimeout,   ///< 超时，没等到终结行
};


/** 一次 AT 命令的结果。line 保存终结行之前的最后一条其它行。 */
struct AtResult
{
    AtStatus status;
    char line[AT_LINE_MAX];
};


/** 模块主动上报（URC）的类别。 */
enum class AtEvent : uint8_t
{
    kMqttConnected,      ///< +MQTTCONNECTED
    kMqttDisconnected,   ///< +MQTTDISCONNECTED
    kMqttSubReceive,     ///< +MQTTSUBRECV，带 topic 与 payload
    kWifiConnected,      ///< WIFI CONNECTED
    kWifiGotIp,          ///< WIFI GOT IP
    kWifiDisconnected,   ///< WIFI DISCONNECT
};


/** 一条待处理的 URC。topic 与 payload 只对 kMqttSubReceive 有意义。 */
struct AtUrc
{
    AtEvent event;
    char topic[AT_TOPIC_MAX];
    uint16_t length;
    uint8_t payload[AT_PAYLOAD_MAX];
};


/**
 * AT 通道：命令行收发、URC 分流、按长度取数据。
 *
 * 这一层只认识"行"和"OK/ERROR/FAIL/>"，不认识 Wi-Fi 和 MQTT 的语义；
 * 例外是 `+MQTTSUBRECV`——它的数据段不带行结束符，只能按长度截取，
 * 所以必须在读流的时候就把边界切出来，这是唯一一处协议细节下渗。
 *
 * 线程约定：只在通信线程里调用。BspUart 同样要求单任务独占。
 */
class AtChannel
{
    public:
    /** 打开串口并清空状态。必须在通信线程里调用一次。 */
    static bool initialize();

    /** 清空接收缓冲与 URC 槽。重连、复位前调用。 */
    static void reset();

    /**
     * 发送一条命令并等待终结行。
     *
     * 命令会被追加 `\r\n`。等待期间收到的 URC 存入槽中，不计入结果；
     * 其它非终结行覆盖 result.line，这样 `AT+CIPSTATUS` 能带出 STATUS:3，
     * `AT+MQTTSUB` 能带出 ALREADY SUBSCRIBE。
     */
    static AtResult execute(const char *command, uint32_t timeoutMs);

    /**
     * 发送命令，等到以 prefix 开头的一行，然后继续等到终结行。
     *
     * 用于响应里既有要的值、又有会覆盖 `execute()` 那唯一一格 `line` 的
     * 附加行的情况：`AT+CIPSTATUS` 在已连接时会先给 `STATUS:3`、
     * 再给 `+CIPSTATUS:...`，用 `execute()` 只能拿到后者。
     *
     * @return 等到 prefix 且终结行是 OK 时返回 true
     */
    static bool executeAndWait(const char *command,
                               const char *prefix,
                               uint32_t timeoutMs,
                               char *lineOut,
                               uint32_t capacity);

    /** 发送裸字节，不加任何结尾（PUBRAW 的数据段用）。 */
    static bool sendRaw(const uint8_t *data, uint32_t length, uint32_t timeoutMs);

    /** 等到以 prefix 开头的一行，并把该行复制到 lineOut。 */
    static bool waitForPrefix(const char *prefix,
                              uint32_t timeoutMs,
                              char *lineOut,
                              uint32_t capacity);

    /** 等一个特定事件。槽里已有的会被先查一遍。 */
    static bool waitForEvent(AtEvent event, uint32_t timeoutMs);

    /**
     * 没有命令在等响应时的轮询：把串口上到达的字节解析成记录，URC 存进槽。
     *
     * 空闲时它会一直阻塞到 timeoutMs，所以调用方直接拿它当轮询节奏；
     * 主线里的普通响应行（OK/ERROR 之类）没有人等，直接丢掉。
     *
     * @return 这一轮读到了至少一条记录时返回 true
     */
    static bool pump(uint32_t timeoutMs);

    /** 取出一条待处理 URC。没有则返回 false。 */
    static bool pollUrc(AtUrc *out);

    /** 因槽被占用而覆盖掉的 URC 数。非 0 说明上层轮询得太慢。 */
    static uint32_t droppedUrcCount();
};


#endif //SELFSERVICEGREENHOUSE_FIRMWARE_ATCHANNEL_H
