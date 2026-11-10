//
// Created by APairOfMoons on 2026/11/8.
//

#include "CommunicationService.h"

#include <cstring>

#include "communication/domain/ConfigurationPayload.h"
#include "communication/domain/TextWriter.h"
#include "communication/domain/TopicBuilder.h"
#include "communication/port/CommRtos.h"
#include "communication/transport/AtChannel.h"
#include "communication/transport/EspAtMqtt.h"
#include "communication/transport/EspAtWifi.h"
#include "configuration/CommConfiguration.h"
#include "configuration/LogicalConfiguration.h"


/*
 * 调试器可见的诊断量。
 *
 * 裸机没有日志通道（SCI0 被 ESP 占着），上板联调时只能用 J-Link/GDB 读内存。
 * 这些变量用 C 链接，符号名就是字面上的名字，不必和 C++ 修饰名打交道。
 * 它们只供观察，不参与任何逻辑判断。
 */
extern "C"
{
volatile uint8_t  g_commState   = 0U;   /* CommunicationService::State 的镜像 */
volatile uint8_t  g_commInit    = 0U;   /* 0=未尝试 1=成功 2=失败（串口或队列） */
volatile uint32_t g_commSent    = 0U;   /* 已交给模块的报文 */
volatile uint32_t g_commDropped = 0U;   /* 队列满丢弃 */
volatile uint32_t g_commFailed  = 0U;   /* 发布失败 */
}


namespace
{

/* 上行队列深度。间隔 >1 分钟，8 条足够覆盖一次断线重连的窗口。 */
constexpr uint32_t kQueueDepth = 8U;

/* 在线时空转一轮的最长时间：也是上行消息与 URC 的最大延迟 */
constexpr uint32_t kIdlePollMs = 50U;

/* 上电等模块启动。ESP-01s 从供电到能应答 AT 大约需要几百毫秒。 */
constexpr uint32_t kBootSettleMs = 500U;

constexpr uint32_t kJoinTimeoutMs = 15000U;
constexpr uint32_t kConnectTimeoutMs = 60000U;
constexpr uint32_t kPublishTimeoutMs = 20000U;
constexpr uint32_t kRetryDelayMs = 200U;
constexpr uint32_t kFaultHoldMs = 5000U;

constexpr uint32_t kBootRetries = 3U;
constexpr uint32_t kWifiRetries = 3U;
constexpr uint32_t kBootRoundsBeforeFault = 5U;
constexpr uint32_t kPublishFailuresBeforeReconnect = 3U;

/* LWT 报文与协议 v1 一致；configure() 会替我们转义其中的引号。 */
constexpr const char *kLwtPayload = "{\"v\":1,\"online\":false}";


CommRtosQueue s_queue = nullptr;
volatile CommunicationService::State s_state = CommunicationService::State::kBooting;
bool s_initialized = false;

uint32_t s_bootRounds = 0U;
uint32_t s_backoffMs = 0U;
uint32_t s_publishFailures = 0U;

/* 拼 topic 与 ack 用的缓冲。单任务独占，放静态区省栈。 */
char s_topic[64];
char s_ack[96];


bool publishPayload(const char *topic,
                    const char *payload,
                    uint32_t length,
                    uint8_t qos,
                    bool retain)
{
    if ((topic == nullptr) || (payload == nullptr) || (length == 0U))
    {
        return false;
    }

    return EspAtMqtt::publish(topic,
                              reinterpret_cast<const uint8_t *>(payload),
                              length,
                              qos,
                              retain,
                              kPublishTimeoutMs);
}


void stepBooting()
{
    /*
     * 模块冷启动需要时间，只在第一次进这里等。之后从 kFault 回来时
     * 模块已经跑了一会儿，再等就纯属浪费。
     */
    if (s_bootRounds == 0U)
    {
        commRtosDelay(kBootSettleMs);
    }

    AtChannel::reset();

    for (uint32_t attempt = 0U; attempt < kBootRetries; ++attempt)
    {
        if (EspAtWifi::ping(COMM_CONFIGURATION.AT_TIMEOUT_MS))
        {
            s_bootRounds = 0U;
            s_state = CommunicationService::State::kModuleReady;
            return;
        }

        commRtosDelay(kRetryDelayMs);
    }

    ++s_bootRounds;
    if (s_bootRounds >= kBootRoundsBeforeFault)
    {
        s_bootRounds = 0U;
        s_state = CommunicationService::State::kFault;
    }
}


void stepModuleReady()
{
    if (!EspAtWifi::disableEcho(COMM_CONFIGURATION.AT_TIMEOUT_MS) ||
        !EspAtWifi::setStationMode(COMM_CONFIGURATION.AT_TIMEOUT_MS) ||
        !EspAtWifi::enableAutoConnect(COMM_CONFIGURATION.AT_TIMEOUT_MS))
    {
        /* 这些命令在每次复位后都要重发，失败就退回探测 */
        s_state = CommunicationService::State::kBooting;
        return;
    }

    s_state = CommunicationService::State::kWifiReady;
}


void stepWifiReady()
{
    for (uint32_t attempt = 0U; attempt < kWifiRetries; ++attempt)
    {
        /* 模块可能已经自己连上了（CWAUTOCONN），有 IP 就不用重发 CWJAP */
        if (EspAtWifi::isNetworkReady(kJoinTimeoutMs) ||
            EspAtWifi::join(COMM_CONFIGURATION.WIFI_SSID,
                            COMM_CONFIGURATION.WIFI_PASSWORD,
                            kJoinTimeoutMs))
        {
            s_state = CommunicationService::State::kMqttConnecting;
            return;
        }
    }

    s_state = CommunicationService::State::kBooting;
}


void stepMqttConnecting()
{
    if (!TopicBuilder::statusTopic(s_topic, sizeof(s_topic), COMM_CONFIGURATION.DEVICE_ID))
    {
        s_state = CommunicationService::State::kFault;
        return;
    }

    if (!EspAtMqtt::configure(COMM_CONFIGURATION.DEVICE_ID,
                              COMM_CONFIGURATION.DEVICE_ID,
                              COMM_CONFIGURATION.MQTT_PASSWORD,
                              s_topic,
                              kLwtPayload,
                              COMM_CONFIGURATION.AT_TIMEOUT_MS))
    {
        s_state = CommunicationService::State::kBooting;
        return;
    }

    if (EspAtMqtt::connect(COMM_CONFIGURATION.BROKER_HOST,
                           static_cast<uint16_t>(COMM_CONFIGURATION.BROKER_PORT),
                           kConnectTimeoutMs))
    {
        s_backoffMs = 0U;
        s_publishFailures = 0U;
        s_state = CommunicationService::State::kOnline;
        return;
    }

    /* 连接失败时清掉模块里可能残留的半开会话，避免下次 CONN 被拒 */
    (void) EspAtMqtt::cleanSession(COMM_CONFIGURATION.AT_TIMEOUT_MS);
    s_state = CommunicationService::State::kReconnecting;
}


bool enterOnline()
{
    if (!TopicBuilder::statusTopic(s_topic, sizeof(s_topic), COMM_CONFIGURATION.DEVICE_ID))
    {
        return false;
    }

    char payload[64];
    TextWriter writer(payload, sizeof(payload));
    writer.append("{\"v\":1,\"online\":true,\"up\":");
    writer.appendUnsigned(commRtosUptimeSeconds());
    writer.append('}');

    if (writer.isTruncated())
    {
        return false;
    }

    if (!publishPayload(s_topic, payload, static_cast<uint32_t>(writer.length()), 1U, true))
    {
        return false;
    }

    if (!TopicBuilder::configTopic(s_topic, sizeof(s_topic), COMM_CONFIGURATION.DEVICE_ID))
    {
        return false;
    }

    /*
     * 每次上线都重订阅一遍，这样 broker 是否保留会话都不影响配置通道。
     * 订阅失败不影响上报，记下来即可，下次上线再试。
     */
    (void) EspAtMqtt::subscribe(s_topic, COMM_CONFIGURATION.AT_TIMEOUT_MS);

    return true;
}


void handleConfig(const AtUrc &urc)
{
    if (!TopicBuilder::configTopic(s_topic, sizeof(s_topic), COMM_CONFIGURATION.DEVICE_ID))
    {
        return;
    }

    /* 只认自己那条配置 topic，别的（将来若有）忽略 */
    if (std::strcmp(urc.topic, s_topic) != 0)
    {
        return;
    }

    bool accepted = false;
    const char *reason = nullptr;

    /* 客户端只发要改的字段，所以先取当前配置当基线 */
    LogicalStatics current;
    LogicalConfiguration::snapshot(&current);

    if (!ConfigurationPayload::decode(reinterpret_cast<const char *>(urc.payload),
                                      urc.length,
                                      &current.variable))
    {
        reason = "syntax";
    }
    else if (!LogicalConfiguration::modifyConfiguration(current.variable))
    {
        /* 语法对不代表物理上合理，护栏在逻辑侧 */
        reason = "rejected";
    }
    else
    {
        accepted = true;
    }

    const std::size_t ackLength = ConfigurationPayload::buildAck(accepted,
                                                                reason,
                                                                s_ack,
                                                                sizeof(s_ack));
    if (ackLength == 0U)
    {
        return;
    }

    if (!TopicBuilder::configAckTopic(s_topic, sizeof(s_topic), COMM_CONFIGURATION.DEVICE_ID))
    {
        return;
    }

    (void) publishPayload(s_topic, s_ack, static_cast<uint32_t>(ackLength), 1U, false);
}


/** @return 连接仍然可用时返回 true */
bool handleUrc(const AtUrc &urc)
{
    switch (urc.event)
    {
        case AtEvent::kMqttSubReceive:
        {
            handleConfig(urc);
            return true;
        }

        case AtEvent::kMqttDisconnected:
        case AtEvent::kWifiDisconnected:
        {
            return false;
        }

        default:
        {
            return true;
        }
    }
}


void stepOnline()
{
    if (!enterOnline())
    {
        s_state = CommunicationService::State::kReconnecting;
        return;
    }

    for (;;)
    {
        /* 没有命令在等响应时，串口上剩下的只有 URC */
        AtChannel::pump(kIdlePollMs);

        AtUrc urc;
        while (AtChannel::pollUrc(&urc))
        {
            if (!handleUrc(urc))
            {
                s_state = CommunicationService::State::kReconnecting;
                return;
            }
        }

        CommunicationService::OutgoingMessage message;
        while (commRtosQueueReceive(s_queue, &message, 0U))
        {
            if (publishPayload(message.topic,
                               message.payload,
                               message.length,
                               message.qos,
                               message.retain))
            {
                ++g_commSent;
                s_publishFailures = 0U;
            }
            else
            {
                ++g_commFailed;
                ++s_publishFailures;

                if (s_publishFailures >= kPublishFailuresBeforeReconnect)
                {
                    s_state = CommunicationService::State::kReconnecting;
                    return;
                }
            }
        }
    }
}


void stepReconnecting()
{
    /* 断线期间的积压一律丢掉，恢复后不发送陈旧数据（测试计划 T7） */
    commRtosQueueReset(s_queue);

    s_backoffMs = (s_backoffMs == 0U) ? COMM_CONFIGURATION.RECONNECT_MIN_MS
                                     : (s_backoffMs * 2U);

    if (s_backoffMs > COMM_CONFIGURATION.RECONNECT_MAX_MS)
    {
        s_backoffMs = COMM_CONFIGURATION.RECONNECT_MAX_MS;
    }

    commRtosDelay(s_backoffMs);
    s_state = CommunicationService::State::kMqttConnecting;
}


void stepFault()
{
    /* 连 AT 都不应答了，只能硬复位模块 */
    (void) EspAtWifi::reset(COMM_CONFIGURATION.AT_TIMEOUT_MS);

    s_bootRounds = 0U;
    commRtosDelay(kFaultHoldMs);
    s_state = CommunicationService::State::kBooting;
}

} // namespace


bool CommunicationService::initialize()
{
    if (s_initialized)
    {
        return true;
    }

    if (!AtChannel::initialize())
    {
        g_commInit = 2U;
        return false;
    }

    s_queue = commRtosQueueCreate(kQueueDepth, static_cast<uint32_t>(sizeof(OutgoingMessage)));
    if (s_queue == nullptr)
    {
        g_commInit = 2U;
        return false;
    }

    s_initialized = true;
    g_commInit = 1U;
    return true;
}


void CommunicationService::run()
{
    if (!initialize())
    {
        /*
         * 串口打不开或队列建不起来属于静态配置错误，没有可恢复的路径。
         * 保持线程存活而不是返回，避免 FreeRTOS 对任务返回做异常处理。
         */
        for (;;)
        {
            commRtosDelay(1000U);
        }
    }

    for (;;)
    {
        /* 状态镜像给调试器看。stepOnline 内部不返回，所以在分派前同步就够了。 */
        g_commState = static_cast<uint8_t>(s_state);

        switch (s_state)
        {
            case State::kBooting:
            {
                stepBooting();
                break;
            }

            case State::kModuleReady:
            {
                stepModuleReady();
                break;
            }

            case State::kWifiReady:
            {
                stepWifiReady();
                break;
            }

            case State::kMqttConnecting:
            {
                stepMqttConnecting();
                break;
            }

            case State::kOnline:
            {
                stepOnline();
                break;
            }

            case State::kReconnecting:
            {
                stepReconnecting();
                break;
            }

            default:
            {
                stepFault();
                break;
            }
        }
    }
}


bool CommunicationService::submit(const OutgoingMessage &message)
{
    if ((s_queue == nullptr) || (message.length == 0U) ||
        (message.length > sizeof(message.payload)))
    {
        return false;
    }

    if (!commRtosQueueSend(s_queue, &message, 0U))
    {
        /* 逻辑侧拿不到失败信号（接口是 void），所以计数留在这里给诊断看 */
        commRtosEnterCritical();
        ++g_commDropped;
        commRtosExitCritical();
        return false;
    }

    return true;
}


CommunicationService::State CommunicationService::state()
{
    return s_state;
}


uint32_t CommunicationService::sentCount()
{
    return g_commSent;
}


uint32_t CommunicationService::droppedCount()
{
    return g_commDropped;
}


uint32_t CommunicationService::failedCount()
{
    return g_commFailed;
}
