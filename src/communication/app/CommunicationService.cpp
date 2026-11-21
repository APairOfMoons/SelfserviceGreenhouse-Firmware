//
// Created by APairOfMoons on 2026/11/8.
//

#include "CommunicationService.h"

#include <cstring>

#include "communication/domain/BootSession.h"
#include "communication/domain/ConfigurationPayload.h"
#include "communication/domain/TextWriter.h"
#include "communication/domain/TopicBuilder.h"
#include "communication/port/CommRtos.h"
#include "communication/port/EntropySource.h"
#include "communication/transport/AtChannel.h"
#include "communication/transport/EspAtMqtt.h"
#include "communication/transport/EspAtWifi.h"
#include "configuration/CommConfiguration.h"
#include "configuration/LogicalConfiguration.h"
#include "configuration/RtcClock.h"

#ifndef GREENHOUSE_COMM_RTT_LOG
#define GREENHOUSE_COMM_RTT_LOG 1
#endif

#if GREENHOUSE_COMM_RTT_LOG
#include "rtt/SEGGER_RTT.h"
#define COMM_RTT_LOG(...) (void) SEGGER_RTT_printf(0U, __VA_ARGS__)
#else
#define COMM_RTT_LOG(...) ((void) 0)
#endif


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
constexpr uint32_t kSubscribeTimeoutMs = 15000U;
constexpr uint32_t kPublishTimeoutMs = 20000U;
constexpr uint32_t kRetryDelayMs = 200U;
constexpr uint32_t kFaultHoldMs = 5000U;

constexpr uint32_t kBootRetries = 3U;
constexpr uint32_t kWifiRetries = 3U;
constexpr uint32_t kBootRoundsBeforeFault = 5U;
constexpr uint32_t kPublishFailuresBeforeReconnect = 3U;

constexpr int8_t   kSntpTimezoneHours = 8;
constexpr uint32_t kSntpUnsyncedPollMs = 5000U;
constexpr uint32_t kSntpResyncIntervalMs = 3600000U;

/*
 * LWT 报文。
 *
 * 只能是纯文本：这块固件的 CONNCFG 收不下带转义双引号的 JSON——实测 75 B 的
 * `{"v":2,"online":false}` 回 ERROR，56 B 的 `offline` 通过（docs/AT_COMMAND_SET.md
 * §4.1）。所以 status 上有两种载荷形态，消费方按首个字符区分。
 *
 * 载荷在连接时登记、运行时改不了，因此遗嘱里也没有 b——离线帧不带会话号，
 * 这是已知且无害的不对称。
 */
constexpr const char *kLwtPayload = "offline";


CommRtosQueue s_queue = nullptr;
volatile CommunicationService::State s_state = CommunicationService::State::kBooting;
bool s_initialized = false;

uint32_t s_bootRounds = 0U;
uint32_t s_backoffMs = 0U;
uint32_t s_publishFailures = 0U;

bool s_sntpConfigured = false;
bool s_sntpSynced = false;
bool s_hasSntpPolled = false;
uint32_t s_lastSntpPollMs = 0U;

/* 拼 topic 与 ack 用的缓冲。单任务独占，放静态区省栈。 */
char s_topic[64];
char s_ack[96];

#if GREENHOUSE_COMM_RTT_LOG
const char *stateName(CommunicationService::State state)
{
    switch (state)
    {
        case CommunicationService::State::kBooting:        return "BOOT";
        case CommunicationService::State::kModuleReady:    return "MOD";
        case CommunicationService::State::kWifiReady:      return "WIFI";
        case CommunicationService::State::kMqttConnecting: return "CONN";
        case CommunicationService::State::kOnline:         return "ONLN";
        case CommunicationService::State::kReconnecting:   return "RECN";
        case CommunicationService::State::kFault:          return "FALT";
        default:                                           return "?";
    }
}
#endif


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
    /* 这两条每次复位后都要重发，失败就退回探测 */
    if (!EspAtWifi::disableEcho(COMM_CONFIGURATION.AT_TIMEOUT_MS) ||
        !EspAtWifi::setStationMode(COMM_CONFIGURATION.AT_TIMEOUT_MS))
    {
        s_state = CommunicationService::State::kBooting;
        return;
    }

    /*
     * 自动重连是锦上添花，不是前置条件：老固件没有 CWAUTOCONN，它会回 ERROR。
     * 从前把它和上面两条并列，一条不支持就永远进不了 Wi-Fi 阶段，
     * 现象恰好是"卡在关联热点"（TROUBLESHOOTING.md §2.2）。kWifiReady 里本来
     * 就会显式 CWJAP，少了它只是每次复位多一条命令。
     */
    (void) EspAtWifi::enableAutoConnect(COMM_CONFIGURATION.AT_TIMEOUT_MS);

    /* 每次模块初始化/复位后都需在 Wi-Fi 就绪时重新下发 SNTP 配置。 */
    s_sntpConfigured = false;
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
            if (!s_sntpConfigured)
            {
                /* 尽最大努力配置 SNTP，不因失败阻塞后续 MQTT 建连。 */
                s_sntpConfigured = EspAtWifi::configureSntp(kSntpTimezoneHours,
                                                            nullptr,
                                                            nullptr,
                                                            COMM_CONFIGURATION.AT_TIMEOUT_MS);
            }

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

    /*
     * 先清一次会话再配置。
     *
     * 模块把订阅的过滤器存在自己那边，重连时并不会因为 Broker 侧 clean session
     * 就丢掉；同一 topic 订两次会让每条下行报文回调两次（docs/AT_COMMAND_SET.md
     * §4.3）。我们的流程是每次上线都重订阅，所以这里必须先把残留清掉，
     * 否则配置报文会被重复应用、ack 也会发两遍。
     * 未连接时它同样回 OK，失败也无所谓——下一步的 CONN 才是真正的判据。
     */
    (void) EspAtMqtt::cleanSession(COMM_CONFIGURATION.AT_TIMEOUT_MS);

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

    /*
     * 丢掉建连前遗留的 URC（例如 CWJAP 留下的 WIFI CONNECTED/GOT IP，以及
     * 上面 AT+MQTTCLEAN=0 关闭旧会话时模块吐出的 +MQTTDISCONNECTED），避免
     * 它们带进 stepOnline() 误判为新会话刚上线就断线。
     */
    AtChannel::clearUrcs();

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

    char nonceHex[9];
    char payload[64];

    TextWriter writer(payload, sizeof(payload));
    writer.append("{\"v\":2,");

    /*
     * 正常不会走到 else：initialize() 在 run() 的第一条语句里就建立了标识。
     * 万一出了问题，状态报文宁可少一个字段也不发假的 0——它没有 seq，
     * 缺 b 不影响下游去重。
     */
    if (BootSession::format(nonceHex, sizeof(nonceHex)))
    {
        writer.append("\"b\":\"");
        writer.append(nonceHex);
        writer.append("\",");
    }

    writer.append("\"online\":true,\"up\":");
    writer.appendUnsigned(commRtosUptimeSeconds());
    writer.append('}');

    if (writer.isTruncated())
    {
        return false;
    }

    const bool statusOk = publishPayload(s_topic, payload, static_cast<uint32_t>(writer.length()), 1U, true);
    COMM_RTT_LOG("[comm %s] status pub %s len=%u\r\n",
                 RtcClock::logTime(commRtosUptimeMs()).text,
                 statusOk ? "ok" : "fail",
                 static_cast<unsigned>(writer.length()));
    if (!statusOk)
    {
        return false;
    }

    if (!TopicBuilder::configTopic(s_topic, sizeof(s_topic), COMM_CONFIGURATION.DEVICE_ID))
    {
        return false;
    }

    /*
     * 每次上线都重订阅一遍，这样 broker 是否保留会话都不影响配置通道。
     * SUB 是网络往返（等 SUBACK），必须给足网络超时；若发生超时，模块可能
     * 仍在处理 SUB（随后发 PUBRAW 会触发 busy p... 错位），直接退回重连。
     * 若模块明确回绝但连接仍健在，则不影响上行遥测。
     */
    const bool subOk = EspAtMqtt::subscribe(s_topic, kSubscribeTimeoutMs);
    (void) subOk;
    COMM_RTT_LOG("[comm %s] cfg sub %s\r\n",
                 RtcClock::logTime(commRtosUptimeMs()).text,
                 subOk ? "ok" : "fail");

    if (!subOk && (g_mqttFailStatus == static_cast<uint8_t>(AtStatus::kTimeout)))
    {
        return false;
    }

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
            COMM_RTT_LOG("[comm %s] urc sub len=%u\r\n",
                         RtcClock::logTime(commRtosUptimeMs()).text,
                         static_cast<unsigned>(urc.length));
            handleConfig(urc);
            return true;
        }

        case AtEvent::kMqttDisconnected:
        {
            COMM_RTT_LOG("[comm %s] urc mqtt disc\r\n",
                         RtcClock::logTime(commRtosUptimeMs()).text);
            return false;
        }

        case AtEvent::kWifiDisconnected:
        {
            COMM_RTT_LOG("[comm %s] urc wifi disc\r\n",
                         RtcClock::logTime(commRtosUptimeMs()).text);
            return false;
        }

        default:
        {
            return true;
        }
    }
}


void pollSntpIfDue()
{
    const uint32_t nowMs = commRtosUptimeMs();
    const bool needInitialSync = (!s_sntpSynced) || (!RtcClock::isSynced());
    const uint32_t intervalMs = needInitialSync ? kSntpUnsyncedPollMs : kSntpResyncIntervalMs;

    if (s_hasSntpPolled && ((nowMs - s_lastSntpPollMs) < intervalMs))
    {
        return;
    }

    if (!s_sntpConfigured)
    {
        s_sntpConfigured = EspAtWifi::configureSntp(kSntpTimezoneHours,
                                                    nullptr,
                                                    nullptr,
                                                    COMM_CONFIGURATION.AT_TIMEOUT_MS);
        if (!s_sntpConfigured)
        {
            s_hasSntpPolled = true;
            s_lastSntpPollMs = commRtosUptimeMs();
            return;
        }
    }

    RtcDateTime dt = {};
    if (EspAtWifi::querySntpTime(&dt, COMM_CONFIGURATION.AT_TIMEOUT_MS))
    {
        const uint32_t syncUptimeMs = commRtosUptimeMs();
        if (RtcClock::updateFromSntp(dt, syncUptimeMs))
        {
            s_sntpSynced = true;
            s_hasSntpPolled = true;
            s_lastSntpPollMs = syncUptimeMs;
            COMM_RTT_LOG("[comm %s] sntp sync %04u-%02u-%02u %02u:%02u:%02u\r\n",
                         RtcClock::logTime(syncUptimeMs).text,
                         static_cast<unsigned>(dt.year),
                         static_cast<unsigned>(dt.month),
                         static_cast<unsigned>(dt.day),
                         static_cast<unsigned>(dt.hour),
                         static_cast<unsigned>(dt.minute),
                         static_cast<unsigned>(dt.second));
            return;
        }
    }

    /* 未同步（1970）或 AT 超时时不破坏 MQTT 连接，等待下一轮周期再查询。 */
    s_hasSntpPolled = true;
    s_lastSntpPollMs = commRtosUptimeMs();
}


void stepOnline()
{
    if (!enterOnline())
    {
        COMM_RTT_LOG("[comm %s] exit online: enter fail\r\n",
                     RtcClock::logTime(commRtosUptimeMs()).text);
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
                COMM_RTT_LOG("[comm %s] exit online: urc disc\r\n",
                             RtcClock::logTime(commRtosUptimeMs()).text);
                s_state = CommunicationService::State::kReconnecting;
                return;
            }
        }

        CommunicationService::OutgoingMessage message;
        while (commRtosQueueReceive(s_queue, &message, 0U))
        {
            const bool pubOk = publishPayload(message.topic,
                                              message.payload,
                                              message.length,
                                              message.qos,
                                              message.retain);
            if (pubOk)
            {
                ++g_commSent;
                s_publishFailures = 0U;
            }
            else
            {
                ++g_commFailed;
                ++s_publishFailures;
            }

            COMM_RTT_LOG("[comm %s] pub %s l=%u %s s=%u f=%u d=%u\r\n",
                         RtcClock::logTime(commRtosUptimeMs()).text,
                         message.topic,
                         static_cast<unsigned>(message.length),
                         pubOk ? "ok" : "fail",
                         static_cast<unsigned>(g_commSent),
                         static_cast<unsigned>(g_commFailed),
                         static_cast<unsigned>(g_commDropped));

            if (s_publishFailures >= kPublishFailuresBeforeReconnect)
            {
                COMM_RTT_LOG("[comm %s] exit online: pub fail=%u\r\n",
                             RtcClock::logTime(commRtosUptimeMs()).text,
                             static_cast<unsigned>(s_publishFailures));
                s_state = CommunicationService::State::kReconnecting;
                return;
            }

            if (!pubOk)
            {
                /*
                 * 发布失败时立刻收一次串口并检查 URC：如果底层已经收到
                 * +MQTTDISCONNECTED / WIFI DISCONNECT，马上退出在线态，
                 * 避免继续消耗队列再做无谓的失败重试。
                 */
                AtChannel::pump(10U);
                while (AtChannel::pollUrc(&urc))
                {
                    if (!handleUrc(urc))
                    {
                        COMM_RTT_LOG("[comm %s] exit online: pub urc disc\r\n",
                                     RtcClock::logTime(commRtosUptimeMs()).text);
                        s_state = CommunicationService::State::kReconnecting;
                        return;
                    }
                }
            }
        }

        pollSntpIfDue();
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

    s_sntpConfigured = false;
    s_sntpSynced = false;
    s_hasSntpPolled = false;

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

    /*
     * 上电标识要早于任何发送。这里已经处在调度器上下文（由 comm_thread 调进来），
     * 所以取得到 tick，也允许被中断打断——中断落在采样之间正是熵的一部分。
     */
    BootSession::establish(BootSession::mix(entropySystickPhase(),
                                            commRtosUptimeMs(),
                                            entropyStackAddress()));

    if (!AtChannel::initialize())
    {
        g_commInit = 2U;
        COMM_RTT_LOG("[comm %s] init fail (uart)\r\n",
                     RtcClock::logTime(commRtosUptimeMs()).text);
        return false;
    }

    s_queue = commRtosQueueCreate(kQueueDepth, static_cast<uint32_t>(sizeof(OutgoingMessage)));
    if (s_queue == nullptr)
    {
        g_commInit = 2U;
        COMM_RTT_LOG("[comm %s] init fail (queue)\r\n",
                     RtcClock::logTime(commRtosUptimeMs()).text);
        return false;
    }

    s_initialized = true;
    g_commInit = 1U;

#if GREENHOUSE_COMM_RTT_LOG
    char nonceHex[9];
    if (!BootSession::format(nonceHex, sizeof(nonceHex)))
    {
        nonceHex[0] = '?';
        nonceHex[1] = '\0';
    }
    COMM_RTT_LOG("[comm %s] init ok b=%s\r\n",
                 RtcClock::logTime(commRtosUptimeMs()).text,
                 nonceHex);
#endif
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

    COMM_RTT_LOG("[comm %s] state=%s\r\n",
                 RtcClock::logTime(commRtosUptimeMs()).text,
                 stateName(s_state));

    for (;;)
    {
        /* 状态镜像给调试器看。stepOnline 内部不返回，所以在分派前同步就够了。 */
        const State prevState = s_state;
        g_commState = static_cast<uint8_t>(prevState);

        switch (prevState)
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

        const State nextState = s_state;
        if (nextState != prevState)
        {
            COMM_RTT_LOG("[comm %s] %s -> %s\r\n",
                         RtcClock::logTime(commRtosUptimeMs()).text,
                         stateName(prevState),
                         stateName(nextState));
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
