//
// Created by APairOfMoons on 2026/11/3.
//

#include "MessageSender.h"

#include <cstring>

#include "CommunicationService.h"
#include "communication/domain/AlarmPayload.h"
#include "communication/domain/TelemetryPayload.h"
#include "communication/domain/TopicBuilder.h"
#include "communication/port/CommRtos.h"
#include "configuration/CommConfiguration.h"


namespace
{

/*
 * 遥测序号。只在逻辑线程里自增，不需要保护——通信模块约定上报由业务触发、
 * 间隔 >1 分钟，不存在并发调用。
 */
uint32_t s_sequence = 0U;

/*
 * 逻辑线程（new_thread0）的栈只有 1024 B，放不下 500 多字节的局部缓冲，
 * 所以报文与队列元素都放静态区。它们只在同一个线程里顺序使用，不会重入。
 */
char s_payload[224];
CommunicationService::OutgoingMessage s_message;

/* 把 s_payload 里已编码好的内容装进 s_message 并交给通信线程 */
void submitCurrent(const std::size_t length, bool retain)
{
    std::memcpy(s_message.payload, s_payload, length);
    s_message.length = static_cast<uint16_t>(length);
    s_message.qos = 1U;
    s_message.retain = retain;

    /* 队列满时丢帧，逻辑侧拿不到失败信号，计数留在通信侧 */
    (void) CommunicationService::submit(s_message);
}

} // namespace


void MessageSender::uploadMonitoringData(MonitoringData monitoringData)
{
    const std::size_t length = TelemetryPayload::build(monitoringData,
                                                       s_sequence,
                                                       commRtosUptimeSeconds(),
                                                       s_payload,
                                                       sizeof(s_payload));
    if (length == 0U)
    {
        return;
    }

    ++s_sequence;

    if (!TopicBuilder::telemetryTopic(s_message.topic,
                                      sizeof(s_message.topic),
                                      COMM_CONFIGURATION.DEVICE_ID))
    {
        return;
    }

    submitCurrent(length, false);
}


void MessageSender::throwGreenhouseException(GreenhouseException exception)
{
    const std::size_t length = AlarmPayload::build(exception,
                                                   commRtosUptimeSeconds(),
                                                   s_payload,
                                                   sizeof(s_payload));
    if (length == 0U)
    {
        return;
    }

    if (!TopicBuilder::alarmTopic(s_message.topic,
                                  sizeof(s_message.topic),
                                  COMM_CONFIGURATION.DEVICE_ID,
                                  AlarmPayload::code(exception)))
    {
        return;
    }

    /* 告警 retain：客户端重连后能直接看到当前有哪些告警 */
    submitCurrent(length, true);
}
