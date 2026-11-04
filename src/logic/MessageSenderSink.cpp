//
// MessageSender 适配实现。
//

#include "logic/MessageSenderSink.h"

namespace greenhouse {

MessageSenderSink::MessageSenderSink(MessageSender &sender)
    : sender_(sender)
{
}

bool MessageSenderSink::publish(const MonitoringData &data)
{
    // uploadMonitoringData() 返回 void，无法感知链路是否就绪，
    // 因此这里一律认为"已交给通信侧"；排队/丢帧策略由 MessageSender 内部处理。
    sender_.uploadMonitoringData(data);
    return true;
}

} // namespace greenhouse
