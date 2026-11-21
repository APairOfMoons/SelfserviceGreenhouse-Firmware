//
// Created by APairOfMoons on 2026/11/8.
//

#include "EspAtMqtt.h"

#include <cstring>

#include "AtChannel.h"
#include "communication/domain/TextWriter.h"
#include "communication/port/CommRtos.h"

#ifndef GREENHOUSE_COMM_RTT_LOG
#define GREENHOUSE_COMM_RTT_LOG 1
#endif

#if GREENHOUSE_COMM_RTT_LOG
#include "rtt/SEGGER_RTT.h"
#define MQTT_RTT_LOG(...) (void) SEGGER_RTT_printf(0U, __VA_ARGS__)
#else
#define MQTT_RTT_LOG(...) ((void) 0)
#endif


extern "C"
{
volatile uint8_t g_mqttFailStep = 0U;
volatile uint8_t g_mqttFailStatus = 0U;
char g_mqttFailLine[AT_LINE_MAX] = {0};
}


namespace
{

/* 发送一帧 PUBRAW 数据时等串口发完的上限。450 字节在 115200 下约 40 ms。 */
constexpr uint32_t kRawWriteTimeoutMs = 2000U;

/* MQTT keepalive，单位秒 */
constexpr uint32_t kKeepAliveSeconds = 120U;

/*
 * 拼命令用的缓冲。单任务独占，放静态区而不是栈上——comm_thread 只有 3 KB 栈，
 * 而这条缓冲要贯穿整个 execute() 调用的栈深度。
 */
char s_commandBuffer[AT_COMMAND_MAX];


/*
 * 这个字符能不能安全地放进带引号的 AT 字符串参数。
 *
 * AT 命令用双引号界定参数，而这块固件的解析器**不处理反斜杠转义**（实测见
 * docs/AT_COMMAND_SET.md §4.1）。所以参数里出现 `"` 或 `\` 就没有正确的写法，
 * 只能拒绝——静默地塞进去，模块要么回 ERROR，要么把后半截当新命令解析。
 */
bool hasReservedChar(const char *text)
{
    if (text == nullptr)
    {
        return true;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor)
    {
        if ((*cursor == '"') || (*cursor == '\\'))
        {
            return true;
        }
    }

    return false;
}


/* 写一个带引号的 AT 字符串参数。参数非法或缓冲区不够时返回 false。 */
bool appendQuoted(TextWriter &writer, const char *text)
{
    if (hasReservedChar(text))
    {
        return false;
    }

    return writer.append('"') && writer.append(text) && writer.append('"');
}


#if GREENHOUSE_COMM_RTT_LOG
const char *failStepName(MqttFailStep step)
{
    switch (step)
    {
        case MqttFailStep::kNone:      return "NONE";
        case MqttFailStep::kParam:     return "PARAM";
        case MqttFailStep::kUserCfg:   return "USERCFG";
        case MqttFailStep::kConnCfg:   return "CONNCFG";
        case MqttFailStep::kConnect:   return "CONN";
        case MqttFailStep::kSubscribe: return "SUB";
        case MqttFailStep::kPublish:   return "PUB";
        default:                       return "?";
    }
}


const char *atStatusName(AtStatus status)
{
    switch (status)
    {
        case AtStatus::kOk:      return "OK";
        case AtStatus::kError:   return "ERR";
        case AtStatus::kFail:    return "FAIL";
        case AtStatus::kPrompt:  return "PROMPT";
        case AtStatus::kTimeout: return "TMO";
        default:                 return "?";
    }
}
#endif


/* 把失败现场留下来：哪一步、模块怎么回的。 */
void recordFailure(MqttFailStep step, AtStatus status, const char *line)
{
    g_mqttFailStep = static_cast<uint8_t>(step);
    g_mqttFailStatus = static_cast<uint8_t>(status);

    if (line == nullptr)
    {
        g_mqttFailLine[0] = '\0';
    }
    else
    {
        std::memcpy(g_mqttFailLine, line, std::strlen(line) + 1U);
    }

    MQTT_RTT_LOG("[mqtt %u] fail %s st=%s ln=%s\r\n",
                 static_cast<unsigned>(commRtosUptimeMs()),
                 failStepName(step),
                 atStatusName(status),
                 (line != nullptr) ? line : "");
}

} // namespace


bool EspAtMqtt::configure(const char *clientId,
                          const char *username,
                          const char *password,
                          const char *lwtTopic,
                          const char *lwtPayload,
                          uint32_t timeoutMs)
{
    /* 每次（重）连都从干净状态开始记失败现场 */
    g_mqttFailStep = static_cast<uint8_t>(MqttFailStep::kNone);

    /*
     * 参数先整体查一遍：错了就一条命令都不发。字符串参数里出现引号或反斜杠
     * 在这块固件上没有正确写法，发出去只会得到 ERROR 或者更糟的半截解析。
     */
    if (hasReservedChar(clientId) || hasReservedChar(username) ||
        hasReservedChar(password) || hasReservedChar(lwtTopic) ||
        hasReservedChar(lwtPayload))
    {
        recordFailure(MqttFailStep::kParam, AtStatus::kError, "BAD_PARAM");
        return false;
    }

    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    /* AT+MQTTUSERCFG=<LinkID>,<scheme>,"<client_id>","<username>","<password>",<cert_id>,<ca_id>,"<path>" */
    const bool userBuilt = writer.append("AT+MQTTUSERCFG=0,1,") &&
                           appendQuoted(writer, clientId) &&
                           writer.append(',') &&
                           appendQuoted(writer, username) &&
                           writer.append(',') &&
                           appendQuoted(writer, password) &&
                           writer.append(",0,0,\"\"");

    if (!userBuilt || writer.isTruncated())
    {
        recordFailure(MqttFailStep::kParam, AtStatus::kError, "BAD_PARAM");
        return false;
    }

    const AtResult userCfg = AtChannel::execute(writer.data(), timeoutMs);
    if (userCfg.status != AtStatus::kOk)
    {
        /* 没有 client id 与凭据就无从连接，这一步是硬失败 */
        recordFailure(MqttFailStep::kUserCfg, userCfg.status, userCfg.line);
        return false;
    }

    /* AT+MQTTCONNCFG=<LinkID>,<keepalive>,<disable_clean_session>,"<lwt_topic>","<lwt_msg>",<lwt_qos>,<lwt_retain> */
    writer.reset();
    const bool connBuilt = writer.append("AT+MQTTCONNCFG=0,") &&
                           writer.appendUnsigned(kKeepAliveSeconds) &&
                           writer.append(",0,") &&
                           appendQuoted(writer, lwtTopic) &&
                           writer.append(',') &&
                           appendQuoted(writer, lwtPayload) &&
                           writer.append(",1,1");

    if (!connBuilt || writer.isTruncated())
    {
        recordFailure(MqttFailStep::kParam, AtStatus::kError, "BAD_PARAM");
        return true;
    }

    const AtResult connCfg = AtChannel::execute(writer.data(), timeoutMs);
    if (connCfg.status != AtStatus::kOk)
    {
        /*
         * 遗嘱登记不上只是"失联时没人替我们喊 offline"，上报本身不受影响。
         * 为一个可选的配置项放弃整条连接不值得，记现场后继续。
         */
        recordFailure(MqttFailStep::kConnCfg, connCfg.status, connCfg.line);
    }

    return true;
}


bool EspAtMqtt::connect(const char *host, uint16_t port, uint32_t timeoutMs)
{
    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    const bool built = writer.append("AT+MQTTCONN=0,") &&
                       appendQuoted(writer, host) &&
                       writer.append(',') &&
                       writer.appendUnsigned(static_cast<uint32_t>(port)) &&
                       writer.append(",0");

    if (!built || writer.isTruncated())
    {
        recordFailure(MqttFailStep::kParam, AtStatus::kError, "BAD_PARAM");
        return false;
    }

    const AtResult result = AtChannel::execute(writer.data(), timeoutMs);
    if (result.status != AtStatus::kOk)
    {
        recordFailure(MqttFailStep::kConnect, result.status, result.line);
        return false;
    }

    /* OK 只代表命令被接受（约束 C5），连上没有要看 URC */
    if (!AtChannel::waitForEvent(AtEvent::kMqttConnected, timeoutMs))
    {
        recordFailure(MqttFailStep::kConnect, AtStatus::kTimeout, "+MQTTCONNECTED");
        return false;
    }

    return true;
}


bool EspAtMqtt::cleanSession(uint32_t timeoutMs)
{
    return AtChannel::execute("AT+MQTTCLEAN=0", timeoutMs).status == AtStatus::kOk;
}


bool EspAtMqtt::subscribe(const char *topic, uint32_t timeoutMs)
{
    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    const bool built = writer.append("AT+MQTTSUB=0,") &&
                       appendQuoted(writer, topic) &&
                       writer.append(",1");

    if (!built || writer.isTruncated())
    {
        recordFailure(MqttFailStep::kParam, AtStatus::kError, "BAD_PARAM");
        return false;
    }

    const AtResult result = AtChannel::execute(writer.data(), timeoutMs);
    if (result.status == AtStatus::kOk)
    {
        return true;
    }

    /* 已经订阅过，效果与重新订阅相同，按成功处理 */
    if (std::strcmp(result.line, "ALREADY SUBSCRIBE") == 0)
    {
        return true;
    }

    recordFailure(MqttFailStep::kSubscribe, result.status, result.line);
    return false;
}


bool EspAtMqtt::publish(const char *topic,
                        const uint8_t *payload,
                        uint32_t length,
                        uint8_t qos,
                        bool retain,
                        uint32_t timeoutMs)
{
    if ((topic == nullptr) || (payload == nullptr) || (length == 0U))
    {
        return false;
    }

    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    /* AT+MQTTPUBRAW=<LinkID>,"<topic>",<length>,<qos>,<retain> */
    const bool built = writer.append("AT+MQTTPUBRAW=0,") &&
                       appendQuoted(writer, topic) &&
                       writer.append(',') &&
                       writer.appendUnsigned(length) &&
                       writer.append(',') &&
                       writer.appendUnsigned(static_cast<uint32_t>(qos)) &&
                       writer.append(',') &&
                       writer.append(retain ? '1' : '0');

    if (!built || writer.isTruncated())
    {
        recordFailure(MqttFailStep::kParam, AtStatus::kError, "BAD_PARAM");
        return false;
    }

    char line[AT_LINE_MAX];
    line[0] = '\0';

    const AtResult result = AtChannel::execute(writer.data(), timeoutMs);
    if (result.status == AtStatus::kOk)
    {
        /* 个别固件版本先回 OK 再给 '>'，两种顺序都要能走 */
        if (!AtChannel::waitForPrefix(">", timeoutMs, line, sizeof(line)))
        {
            /* 没进数据模式就等于什么都没发，通道仍然是同步的 */
            const bool hasErrorLine = (line[0] != '\0');
            MQTT_RTT_LOG("[mqtt %u] pub wait> fail t=%s l=%u\r\n",
                         static_cast<unsigned>(commRtosUptimeMs()),
                         topic,
                         static_cast<unsigned>(length));
            recordFailure(MqttFailStep::kPublish,
                          hasErrorLine ? AtStatus::kError : AtStatus::kTimeout,
                          hasErrorLine ? line : ">");
            return false;
        }
    }
    else if (result.status != AtStatus::kPrompt)
    {
        MQTT_RTT_LOG("[mqtt %u] pub exec fail t=%s l=%u\r\n",
                     static_cast<unsigned>(commRtosUptimeMs()),
                     topic,
                     static_cast<unsigned>(length));
        recordFailure(MqttFailStep::kPublish, result.status, result.line);
        return false;
    }

    if (!AtChannel::sendRaw(payload, length, kRawWriteTimeoutMs))
    {
        MQTT_RTT_LOG("[mqtt %u] pub sendRaw fail l=%u\r\n",
                     static_cast<unsigned>(commRtosUptimeMs()),
                     static_cast<unsigned>(length));
        recordFailure(MqttFailStep::kPublish, AtStatus::kTimeout, "RAWDATA");
        return false;
    }

    line[0] = '\0';
    if (!AtChannel::waitForPrefix("+MQTTPUB:", timeoutMs, line, sizeof(line)))
    {
        /*
         * 载荷已经整帧发出去了（BspUart 一次提交整段，不会只发一半），
         * 所以模块那边不会卡在数据模式；这里丢掉的是"是否被接受"的确认。
         */
        const bool hasErrorLine = (line[0] != '\0');
        MQTT_RTT_LOG("[mqtt %u] pub waitAck fail t=%s l=%u\r\n",
                     static_cast<unsigned>(commRtosUptimeMs()),
                     topic,
                     static_cast<unsigned>(length));
        recordFailure(MqttFailStep::kPublish,
                      hasErrorLine ? AtStatus::kError : AtStatus::kTimeout,
                      hasErrorLine ? line : "+MQTTPUB:");
        return false;
    }

    if (std::strcmp(line, "+MQTTPUB:OK") != 0)
    {
        MQTT_RTT_LOG("[mqtt %u] pub ack=%s t=%s\r\n",
                     static_cast<unsigned>(commRtosUptimeMs()),
                     line,
                     topic);
        recordFailure(MqttFailStep::kPublish, AtStatus::kError, line);
        return false;
    }

    return true;
}
