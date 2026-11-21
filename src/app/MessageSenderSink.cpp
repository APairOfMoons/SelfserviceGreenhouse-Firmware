//
// MessageSender 适配实现。
//

#include "app/MessageSenderSink.h"

namespace greenhouse {

MessageSenderSink::MessageSenderSink(MessageSender &sender)
    : sender_(sender)
{
}

bool MessageSenderSink::publish(const MonitoringData &data)
{
    // BootSession 未就绪或上行队列满/未创建时返回 false，
    // 让 TelemetryReporter 保留待发状态并在下一拍重试。
    return sender_.uploadMonitoringData(data);
}

} // namespace greenhouse
