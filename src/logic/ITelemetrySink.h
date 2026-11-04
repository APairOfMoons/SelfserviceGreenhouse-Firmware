//
// 逻辑侧 -> 通信侧的数据出口。
//
// 分工：
//   * 逻辑侧（本目录）：决定"什么时候推、推什么内容"，调用 publish()；
//   * 通信侧：实现本接口，或直接用本目录的 MessageSenderSink 包装 MessageSender，
//     把 MonitoringData 编码成 MQTT 报文（WiFi -> TCP -> MQTT），
//     负责连接状态、重连、QoS、缓冲。
//
// 约定：
//   1) publish() 不允许阻塞等待网络；缓冲区满或链路断开时立刻返回 false；
//   2) 返回 false 表示"这次没送出去"，逻辑侧只记账并下一拍重试，不做排队；
//   3) 调用频率上限见 TelemetryReporter 的 min_interval_ms。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_ITELEMETRYSINK_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_ITELEMETRYSINK_H

#include "communication/GreenhouseData.h"

namespace greenhouse {

class ITelemetrySink
{
    public:
    virtual ~ITelemetrySink() = default;

    // 尝试把一帧数据交给通信层。
    // 返回 true 表示已接收（不代表已送达云端）；false 表示本次失败。
    virtual bool publish(const MonitoringData &data) = 0;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_ITELEMETRYSINK_H
