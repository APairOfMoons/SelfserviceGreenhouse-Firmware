//
// 上报节律：决定何时把控制快照推给通信层。
//
// 规则（按优先级）：
//   1) 首帧必发（让云端拿到基线状态）；
//   2) 环境量变化超过阈值 -> 立即发；
//   3) 执行器状态变化 -> 立即发（受最小间隔约束）；
//   4) 都没有变化 -> 按 heartbeat_ms 定期发；
//   5) 任何一次都受 min_interval_ms 限流，避免刷屏。
//
// 不在这里做重发/排队：publish() 返回 false 只记 failedCount，
// 下一拍自然重试；离线缓冲由通信侧负责。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_TELEMETRYREPORTER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_TELEMETRYREPORTER_H

#include "logic/ITelemetrySink.h"
#include "logic/LogicController.h"

namespace greenhouse {

struct ReporterConfig
{
    uint32_t heartbeat_ms;      // 无变化时的定期上报间隔
    uint32_t min_interval_ms;   // 两次上报的最小间隔（限流）

    float temperature_delta;    // 超过该变化量才认为"有变化"
    float air_humidity_delta;
    float soil_humidity_delta;
    int illuminance_delta;

    bool report_on_actuator_change; // 风扇/灯/加热状态变化是否立即上报
};

class TelemetryReporter
{
    public:
    TelemetryReporter(ITelemetrySink &sink, const ReporterConfig &config);

    void setConfig(const ReporterConfig &config);
    const ReporterConfig &config() const;

    // 每个控制节拍调用一次（建议 100 ms ~ 1 s）。
    void update(uint32_t now_ms, const ControllerSnapshot &snapshot);

    // 链路恢复后调用：下一拍会立即补发一帧全量，并清空失败计数。
    void reset();

    uint32_t publishedCount() const;
    uint32_t failedCount() const;
    bool hasPublished() const;
    uint32_t lastPublishMs() const;

    private:
    bool needsPublish(const MonitoringData &data, uint32_t now_ms) const;
    void tryPublish(const MonitoringData &data, uint32_t now_ms);
    static bool exceeds(float current, float previous, float delta);
    static bool exceeds(int current, int previous, int delta);

    ITelemetrySink &sink_;
    ReporterConfig config_;

    MonitoringData last_data_;
    uint32_t last_publish_ms_;
    bool has_published_;

    uint32_t published_count_;
    uint32_t failed_count_;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_TELEMETRYREPORTER_H
