//
// Created by APairOfMoons on 2026/11/8.
//

#include "AtChannel.h"

#include <cstring>

#include "communication/port/BspUart.h"
#include "communication/port/CommRtos.h"


/*
 * 调试器可见的 AT 诊断。
 *
 * 上板时最常问的三个问题是：命令发出去了吗、最后发的是哪条、模块怎么回的。
 * 这三个变量分别回答它们，符号是 C 链接的，GDB 里直接 `p g_atLastCommand`。
 * 只供观察，不参与逻辑。
 */
extern "C"
{
volatile uint32_t g_atCommandCount = 0U;
volatile uint8_t  g_atLastStatus   = 0U;   /* 0=OK 1=ERROR 2=FAIL 3=PROMPT 4=TIMEOUT */
char g_atLastCommand[64]           = {0};
char g_atLastLine[AT_LINE_MAX]     = {0};
}


namespace
{

/* 等串口把一帧命令发完的上限。256 字节在 115200 下约 22 ms。 */
constexpr uint32_t kWriteTimeoutMs = 500U;

/* "+MQTTSUBRECV:" 的字符数，用来判断刚读到的一行是不是它 */
constexpr uint32_t kSubReceivePrefixLength = 13U;

/* 拼命令用。单任务独占，放静态区而不是栈上——comm_thread 只有 3 KB 栈。 */
char s_commandBuffer[AT_COMMAND_MAX];

/*
 * URC 只有一格。配置下发间隔以分钟计，一格足够；被覆盖的计入 dropped，
 * 非 0 说明上层轮询得太慢。
 */
AtUrc s_urcSlot;
bool s_urcPending = false;
uint32_t s_urcDropped = 0U;

/* readRecord 解析 +MQTTSUBRECV 时的暂存区，随后立刻搬进 URC 槽 */
char s_subReceiveTopic[AT_TOPIC_MAX];
uint8_t s_subReceivePayload[AT_PAYLOAD_MAX];
uint32_t s_subReceiveLength = 0U;


/* 取一个字节；暂时没有就让出 1 ms。超时返回 false。 */
bool readByteTimed(uint8_t *value, uint32_t start, uint32_t timeoutMs)
{
    for (;;)
    {
        if ((commRtosUptimeMs() - start) >= timeoutMs)
        {
            return false;
        }

        if (BspUart::readByte(value))
        {
            return true;
        }

        commRtosDelay(1U);
    }
}


/*
 * 读 +MQTTSUBRECV 的剩余部分。
 *
 * 进到这里时 buffer 里已经有 "+MQTTSUBRECV:" 这 13 个字节。之后的格式是
 * <LinkID>,"<topic>",<len>,<data>。前导部分照常写进 buffer（上层据此判类），
 * 数据段严格按 <len> 读进独立缓冲——它可能含逗号、引号甚至换行，
 * 按行或按分隔符切都会错（约束 C11）。
 */
bool readSubReceive(char *buffer, uint32_t start, uint32_t timeoutMs)
{
    uint32_t length = kSubReceivePrefixLength;
    uint32_t topicLength = 0U;
    uint32_t expectedLength = 0U;
    uint32_t receivedLength = 0U;
    uint32_t stage = 0U;           /* 0=LinkID 1=topic 2=topic 后的逗号 3=len 4=data */
    bool topicStarted = false;

    for (;;)
    {
        uint8_t byte = 0U;
        if (!readByteTimed(&byte, start, timeoutMs))
        {
            return false;
        }

        /* 数据段不进行缓冲，其余字节原样保留 */
        if (stage != 4U)
        {
            if ((length + 1U) < AT_LINE_MAX)
            {
                buffer[length] = static_cast<char>(byte);
                ++length;
            }
        }

        switch (stage)
        {
            case 0U:
            {
                if (byte == ',')
                {
                    stage = 1U;
                }
                break;
            }

            case 1U:
            {
                if (byte == '"')
                {
                    if (topicStarted)
                    {
                        stage = 2U;
                    }
                    else
                    {
                        topicStarted = true;
                    }
                }
                else if (topicStarted && (topicLength < (AT_TOPIC_MAX - 1U)))
                {
                    s_subReceiveTopic[topicLength] = static_cast<char>(byte);
                    ++topicLength;
                }
                break;
            }

            case 2U:
            {
                /* topic 结束引号之后、长度之前的那一个逗号 */
                if (byte == ',')
                {
                    stage = 3U;
                }
                break;
            }

            case 3U:
            {
                if (byte == ',')
                {
                    stage = 4U;

                    /* 空数据段：不会再有数据字节到来 */
                    if (expectedLength == 0U)
                    {
                        s_subReceiveTopic[topicLength] = '\0';
                        s_subReceiveLength = 0U;
                        buffer[length] = '\0';
                        return true;
                    }
                }
                else if ((byte >= '0') && (byte <= '9'))
                {
                    expectedLength = (expectedLength * 10U) + static_cast<uint32_t>(byte - '0');
                }
                break;
            }

            default:
            {
                if (receivedLength < AT_PAYLOAD_MAX)
                {
                    s_subReceivePayload[receivedLength] = byte;
                }
                ++receivedLength;

                if (receivedLength >= expectedLength)
                {
                    s_subReceiveTopic[topicLength] = '\0';
                    s_subReceiveLength = (receivedLength < AT_PAYLOAD_MAX)
                                             ? receivedLength
                                             : AT_PAYLOAD_MAX;
                    buffer[length] = '\0';
                    return true;
                }
                break;
            }
        }
    }
}


/*
 * 读一条记录到 buffer。
 *
 * 普通响应以 \n 结尾，尾部的 \r 去掉；空行会返回长度为 0 的记录，
 * 由调用方跳过。返回 true 时 isSubReceive 表示这条是 +MQTTSUBRECV 的前导。
 */
bool readRecord(char *buffer, uint32_t timeoutMs, bool *isSubReceive)
{
    const uint32_t start = commRtosUptimeMs();
    uint32_t length = 0U;

    *isSubReceive = false;

    for (;;)
    {
        uint8_t byte = 0U;
        if (!readByteTimed(&byte, start, timeoutMs))
        {
            return false;
        }

        if (byte == '\n')
        {
            if ((length > 0U) && (buffer[length - 1U] == '\r'))
            {
                --length;
            }
            buffer[length] = '\0';
            return true;
        }

        if (length < (AT_LINE_MAX - 1U))
        {
            buffer[length] = static_cast<char>(byte);
            ++length;
        }

        if ((length == kSubReceivePrefixLength) &&
            (std::memcmp(buffer, "+MQTTSUBRECV:", kSubReceivePrefixLength) == 0))
        {
            *isSubReceive = true;
            return readSubReceive(buffer, start, timeoutMs);
        }
    }
}


enum class LineClass : uint8_t
{
    kOk,
    kError,
    kFail,
    kPrompt,
    kUrc,
    kOther,
};


LineClass classify(const char *line, AtEvent *event)
{
    if (std::strcmp(line, "OK") == 0)
    {
        return LineClass::kOk;
    }

    if (std::strcmp(line, "ERROR") == 0)
    {
        return LineClass::kError;
    }

    /* AT+CWJAP 一类命令失败时返回 FAIL，不是 ERROR */
    if (std::strcmp(line, "FAIL") == 0)
    {
        return LineClass::kFail;
    }

    if (line[0] == '>')
    {
        return LineClass::kPrompt;
    }

    /* 个别固件版本用 +MQTTSUB:OK 代替裸 OK，按成功处理 */
    if (std::strncmp(line, "+MQTTSUB:OK", 11U) == 0)
    {
        return LineClass::kOk;
    }

    if (std::strncmp(line, "+MQTTCONNECTED", 14U) == 0)
    {
        *event = AtEvent::kMqttConnected;
        return LineClass::kUrc;
    }

    if (std::strncmp(line, "+MQTTDISCONNECTED", 17U) == 0)
    {
        *event = AtEvent::kMqttDisconnected;
        return LineClass::kUrc;
    }

    if (std::strcmp(line, "WIFI CONNECTED") == 0)
    {
        *event = AtEvent::kWifiConnected;
        return LineClass::kUrc;
    }

    if (std::strcmp(line, "WIFI GOT IP") == 0)
    {
        *event = AtEvent::kWifiGotIp;
        return LineClass::kUrc;
    }

    if (std::strcmp(line, "WIFI DISCONNECT") == 0)
    {
        *event = AtEvent::kWifiDisconnected;
        return LineClass::kUrc;
    }

    return LineClass::kOther;
}


void storeUrc(AtEvent event)
{
    if (s_urcPending)
    {
        ++s_urcDropped;
        return;
    }

    s_urcSlot.event = event;
    s_urcSlot.topic[0] = '\0';
    s_urcSlot.length = 0U;
    s_urcPending = true;
}


void storeSubReceiveUrc()
{
    if (s_urcPending)
    {
        ++s_urcDropped;
        return;
    }

    s_urcSlot.event = AtEvent::kMqttSubReceive;
    std::memcpy(s_urcSlot.topic, s_subReceiveTopic, AT_TOPIC_MAX);
    s_urcSlot.length = static_cast<uint16_t>(s_subReceiveLength);
    std::memcpy(s_urcSlot.payload, s_subReceivePayload, s_subReceiveLength);
    s_urcPending = true;
}


/* 把一次 AT 交互的结果留在 C 链接的变量里，供调试器查看 */
void recordAt(const char *command, AtStatus status, const char *line)
{
    ++g_atCommandCount;

    if (command != nullptr)
    {
        const std::size_t length = std::strlen(command);
        const std::size_t limit = (length < (sizeof(g_atLastCommand) - 1U))
                                      ? length
                                      : (sizeof(g_atLastCommand) - 1U);
        std::memcpy(g_atLastCommand, command, limit);
        g_atLastCommand[limit] = '\0';
    }

    g_atLastStatus = static_cast<uint8_t>(status);

    if (line != nullptr)
    {
        std::memcpy(g_atLastLine, line, std::strlen(line) + 1U);
    }
    else
    {
        g_atLastLine[0] = '\0';
    }
}


bool sendCommand(const char *command)
{
    const std::size_t length = std::strlen(command);
    if ((length + 2U) > AT_COMMAND_MAX)
    {
        return false;
    }

    std::memcpy(s_commandBuffer, command, length);
    s_commandBuffer[length] = '\r';
    s_commandBuffer[length + 1U] = '\n';

    return BspUart::write(reinterpret_cast<const uint8_t *>(s_commandBuffer),
                          static_cast<uint32_t>(length + 2U),
                          kWriteTimeoutMs);
}

} // namespace


bool AtChannel::initialize()
{
    s_urcPending = false;
    s_urcDropped = 0U;
    s_subReceiveLength = 0U;

    return BspUart::initialize();
}


void AtChannel::reset()
{
    BspUart::flushReceive();
    s_urcPending = false;
    s_subReceiveLength = 0U;
}


static AtResult executeInternal(const char *command, uint32_t timeoutMs)
{
    AtResult result;
    result.status = AtStatus::kTimeout;
    result.line[0] = '\0';

    if (!sendCommand(command))
    {
        return result;
    }

    const uint32_t start = commRtosUptimeMs();
    char line[AT_LINE_MAX];

    for (;;)
    {
        const uint32_t elapsed = commRtosUptimeMs() - start;
        if (elapsed >= timeoutMs)
        {
            return result;
        }

        bool isSubReceive = false;
        if (!readRecord(line, timeoutMs - elapsed, &isSubReceive))
        {
            return result;
        }

        if (isSubReceive)
        {
            storeSubReceiveUrc();
            continue;
        }

        if (line[0] == '\0')
        {
            continue;
        }

        AtEvent event = AtEvent::kMqttConnected;
        switch (classify(line, &event))
        {
            case LineClass::kOk:
            {
                result.status = AtStatus::kOk;
                return result;
            }

            case LineClass::kError:
            {
                result.status = AtStatus::kError;
                return result;
            }

            case LineClass::kFail:
            {
                result.status = AtStatus::kFail;
                return result;
            }

            case LineClass::kPrompt:
            {
                result.status = AtStatus::kPrompt;
                return result;
            }

            case LineClass::kUrc:
            {
                storeUrc(event);
                break;
            }

            default:
            {
                /* 非终结行：留着交给调用方看（STATUS:3、ALREADY SUBSCRIBE 等） */
                std::memcpy(result.line, line, std::strlen(line) + 1U);
                break;
            }
        }
    }
}


AtResult AtChannel::execute(const char *command, uint32_t timeoutMs)
{
    const AtResult result = executeInternal(command, timeoutMs);

    recordAt(command, result.status, result.line);

    return result;
}


bool AtChannel::sendRaw(const uint8_t *data, uint32_t length, uint32_t timeoutMs)
{
    return BspUart::write(data, length, timeoutMs);
}


static bool executeAndWaitInternal(const char *command,
                                   const char *prefix,
                                   uint32_t timeoutMs,
                                   char *lineOut,
                                   uint32_t capacity)
{
    if (!sendCommand(command))
    {
        return false;
    }

    const uint32_t start = commRtosUptimeMs();
    const std::size_t prefixLength = std::strlen(prefix);
    bool matched = false;
    char line[AT_LINE_MAX];

    for (;;)
    {
        const uint32_t elapsed = commRtosUptimeMs() - start;
        if (elapsed >= timeoutMs)
        {
            return false;
        }

        bool isSubReceive = false;
        if (!readRecord(line, timeoutMs - elapsed, &isSubReceive))
        {
            return false;
        }

        if (isSubReceive)
        {
            storeSubReceiveUrc();
            continue;
        }

        if (line[0] == '\0')
        {
            continue;
        }

        AtEvent event = AtEvent::kMqttConnected;
        switch (classify(line, &event))
        {
            case LineClass::kOk:
            case LineClass::kError:
            case LineClass::kFail:
            case LineClass::kPrompt:
            {
                return matched;
            }

            case LineClass::kUrc:
            {
                storeUrc(event);
                break;
            }

            default:
            {
                if ((!matched) && (std::strncmp(line, prefix, prefixLength) == 0))
                {
                    matched = true;

                    if ((lineOut != nullptr) && (capacity > 0U))
                    {
                        const std::size_t copyLength = std::strlen(line);
                        const std::size_t limit = (copyLength < (capacity - 1U))
                                                      ? copyLength
                                                      : (capacity - 1U);
                        std::memcpy(lineOut, line, limit);
                        lineOut[limit] = '\0';
                    }
                }
                break;
            }
        }
    }
}


bool AtChannel::executeAndWait(const char *command,
                               const char *prefix,
                               uint32_t timeoutMs,
                               char *lineOut,
                               uint32_t capacity)
{
    const bool matched = executeAndWaitInternal(command, prefix, timeoutMs, lineOut, capacity);

    recordAt(command,
             matched ? AtStatus::kOk : AtStatus::kTimeout,
             (matched && (lineOut != nullptr)) ? lineOut : "");

    return matched;
}


bool AtChannel::waitForPrefix(const char *prefix,
                              uint32_t timeoutMs,
                              char *lineOut,
                              uint32_t capacity)
{
    const uint32_t start = commRtosUptimeMs();
    const std::size_t prefixLength = std::strlen(prefix);
    char line[AT_LINE_MAX];

    for (;;)
    {
        const uint32_t elapsed = commRtosUptimeMs() - start;
        if (elapsed >= timeoutMs)
        {
            return false;
        }

        bool isSubReceive = false;
        if (!readRecord(line, timeoutMs - elapsed, &isSubReceive))
        {
            return false;
        }

        if (isSubReceive)
        {
            storeSubReceiveUrc();
            continue;
        }

        if (line[0] == '\0')
        {
            continue;
        }

        if (std::strncmp(line, prefix, prefixLength) == 0)
        {
            if ((lineOut != nullptr) && (capacity > 0U))
            {
                const std::size_t copyLength = std::strlen(line);
                const std::size_t limit = (copyLength < (capacity - 1U))
                                              ? copyLength
                                              : (capacity - 1U);
                std::memcpy(lineOut, line, limit);
                lineOut[limit] = '\0';
            }
            return true;
        }

        AtEvent event = AtEvent::kMqttConnected;
        if (classify(line, &event) == LineClass::kUrc)
        {
            storeUrc(event);
        }
    }
}


bool AtChannel::waitForEvent(AtEvent event, uint32_t timeoutMs)
{
    if (s_urcPending && (s_urcSlot.event == event))
    {
        s_urcPending = false;
        return true;
    }

    const uint32_t start = commRtosUptimeMs();
    char line[AT_LINE_MAX];

    for (;;)
    {
        const uint32_t elapsed = commRtosUptimeMs() - start;
        if (elapsed >= timeoutMs)
        {
            return false;
        }

        bool isSubReceive = false;
        if (!readRecord(line, timeoutMs - elapsed, &isSubReceive))
        {
            return false;
        }

        if (isSubReceive)
        {
            storeSubReceiveUrc();
            continue;
        }

        if (line[0] == '\0')
        {
            continue;
        }

        AtEvent received = AtEvent::kMqttConnected;
        if (classify(line, &received) == LineClass::kUrc)
        {
            if (received == event)
            {
                return true;
            }
            storeUrc(received);
        }
    }
}


bool AtChannel::pump(uint32_t timeoutMs)
{
    const uint32_t start = commRtosUptimeMs();
    bool received = false;
    char line[AT_LINE_MAX];

    for (;;)
    {
        const uint32_t elapsed = commRtosUptimeMs() - start;
        if (elapsed >= timeoutMs)
        {
            return received;
        }

        bool isSubReceive = false;
        if (!readRecord(line, timeoutMs - elapsed, &isSubReceive))
        {
            return received;
        }

        received = true;

        if (isSubReceive)
        {
            storeSubReceiveUrc();
            continue;
        }

        if (line[0] == '\0')
        {
            continue;
        }

        AtEvent event = AtEvent::kMqttConnected;
        if (classify(line, &event) == LineClass::kUrc)
        {
            storeUrc(event);
        }

        /* 没有命令在等，OK/ERROR 之类的响应行没人要，丢掉 */
    }
}


bool AtChannel::pollUrc(AtUrc *out)
{
    if (!s_urcPending || (out == nullptr))
    {
        return false;
    }

    *out = s_urcSlot;
    s_urcPending = false;
    return true;
}


uint32_t AtChannel::droppedUrcCount()
{
    return s_urcDropped;
}
