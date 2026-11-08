//
// Created by APairOfMoons on 2026/11/8.
//

#include "EspAtMqtt.h"

#include <cstring>

#include "AtChannel.h"
#include "communication/domain/TextWriter.h"


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
 * 写一个带引号的 AT 字符串参数。
 *
 * AT 命令本身以双引号界定参数，所以参数里的引号和反斜杠都要加反斜杠转义。
 * LWT 报文是 JSON，必然含引号，不转义会让整条命令被截断。
 */
bool appendQuoted(TextWriter &writer, const char *text)
{
    if (!writer.append('"'))
    {
        return false;
    }

    for (const char *cursor = text; *cursor != '\0'; ++cursor)
    {
        if ((*cursor == '"') || (*cursor == '\\'))
        {
            if (!writer.append('\\'))
            {
                return false;
            }
        }

        if (!writer.append(*cursor))
        {
            return false;
        }
    }

    return writer.append('"');
}

} // namespace


bool EspAtMqtt::configure(const char *clientId,
                          const char *username,
                          const char *password,
                          const char *lwtTopic,
                          const char *lwtPayload,
                          uint32_t timeoutMs)
{
    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    /* AT+MQTTUSERCFG=<LinkID>,<scheme>,"<client_id>","<username>","<password>",<cert_id>,<ca_id>,"<path>" */
    writer.append("AT+MQTTUSERCFG=0,1,");
    appendQuoted(writer, clientId);
    writer.append(',');
    appendQuoted(writer, username);
    writer.append(',');
    appendQuoted(writer, password);
    writer.append(",0,0,\"\"");

    if (writer.isTruncated())
    {
        return false;
    }

    if (AtChannel::execute(writer.data(), timeoutMs).status != AtStatus::kOk)
    {
        return false;
    }

    /* AT+MQTTCONNCFG=<LinkID>,<keepalive>,<disable_clean_session>,"<lwt_topic>","<lwt_msg>",<lwt_qos>,<lwt_retain> */
    writer.reset();
    writer.append("AT+MQTTCONNCFG=0,");
    writer.appendUnsigned(kKeepAliveSeconds);
    writer.append(",0,");
    appendQuoted(writer, lwtTopic);
    writer.append(',');
    appendQuoted(writer, lwtPayload);
    writer.append(",1,1");

    if (writer.isTruncated())
    {
        return false;
    }

    return AtChannel::execute(writer.data(), timeoutMs).status == AtStatus::kOk;
}


bool EspAtMqtt::connect(const char *host, uint16_t port, uint32_t timeoutMs)
{
    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    writer.append("AT+MQTTCONN=0,");
    appendQuoted(writer, host);
    writer.append(',');
    writer.appendUnsigned(static_cast<uint32_t>(port));
    writer.append(",0");

    if (writer.isTruncated())
    {
        return false;
    }

    if (AtChannel::execute(writer.data(), timeoutMs).status != AtStatus::kOk)
    {
        return false;
    }

    return AtChannel::waitForEvent(AtEvent::kMqttConnected, timeoutMs);
}


bool EspAtMqtt::cleanSession(uint32_t timeoutMs)
{
    return AtChannel::execute("AT+MQTTCLEAN=0", timeoutMs).status == AtStatus::kOk;
}


bool EspAtMqtt::subscribe(const char *topic, uint32_t timeoutMs)
{
    TextWriter writer(s_commandBuffer, sizeof(s_commandBuffer));

    writer.append("AT+MQTTSUB=0,");
    appendQuoted(writer, topic);
    writer.append(",1");

    if (writer.isTruncated())
    {
        return false;
    }

    const AtResult result = AtChannel::execute(writer.data(), timeoutMs);
    if (result.status == AtStatus::kOk)
    {
        return true;
    }

    /* 已经订阅过，效果与重新订阅相同，按成功处理 */
    return std::strcmp(result.line, "ALREADY SUBSCRIBE") == 0;
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
    writer.append("AT+MQTTPUBRAW=0,");
    appendQuoted(writer, topic);
    writer.append(',');
    writer.appendUnsigned(length);
    writer.append(',');
    writer.appendUnsigned(static_cast<uint32_t>(qos));
    writer.append(',');
    writer.append(retain ? '1' : '0');

    if (writer.isTruncated())
    {
        return false;
    }

    const AtResult result = AtChannel::execute(writer.data(), timeoutMs);
    if (result.status == AtStatus::kOk)
    {
        /* 个别固件版本先回 OK 再给 '>'，两种顺序都要能走 */
        if (!AtChannel::waitForPrefix(">", timeoutMs, nullptr, 0U))
        {
            return false;
        }
    }
    else if (result.status != AtStatus::kPrompt)
    {
        return false;
    }

    if (!AtChannel::sendRaw(payload, length, kRawWriteTimeoutMs))
    {
        return false;
    }

    char line[AT_LINE_MAX];
    if (!AtChannel::waitForPrefix("+MQTTPUB:", timeoutMs, line, sizeof(line)))
    {
        return false;
    }

    return std::strcmp(line, "+MQTTPUB:OK") == 0;
}
