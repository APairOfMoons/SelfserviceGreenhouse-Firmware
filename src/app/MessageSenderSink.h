//
// 把通信侧提供的 MessageSender 接到逻辑侧的 ITelemetrySink。
//
// 接口形态：class + 成员函数 uploadMonitoringData(MonitoringData)。
// 注意：跨线程调用时它必须只做入队（非阻塞），真正发送由 comm_thread 负责。
//
// 位置兼容：communication/app/MessageSender.h（若将来移到 app/ 也能识别）。
//
// 用法（固件集成处）：
//   MessageSender sender;
//   greenhouse::MessageSenderSink sink(sender);
//   greenhouse::TelemetryReporter reporter(sink, config);
//   reporter.update(now, ctrl.snapshot());   // 内部调用 sender.uploadMonitoringData()
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_MESSAGESENDERSINK_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_MESSAGESENDERSINK_H

#if defined(__has_include)
#  if __has_include("app/MessageSender.h")
#    include "app/MessageSender.h"
#  elif __has_include("communication/app/MessageSender.h")
#    include "communication/app/MessageSender.h"
#  endif
#else
#  include "communication/app/MessageSender.h"
#endif

#include "logic/ITelemetrySink.h"

namespace greenhouse {

class MessageSenderSink : public ITelemetrySink
{
    public:
    explicit MessageSenderSink(MessageSender &sender);

    bool publish(const MonitoringData &data) override;

    private:
    MessageSender &sender_;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_MESSAGESENDERSINK_H
