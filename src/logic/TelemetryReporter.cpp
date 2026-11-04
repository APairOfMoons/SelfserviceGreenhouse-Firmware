//
// 上报节律实现。
//

#include "logic/TelemetryReporter.h"

#include "logic/MonitoringDataMapper.h"

namespace greenhouse {
namespace {

inline uint32_t elapsed(uint32_t now, uint32_t since)
{
    return now - since;
}

MonitoringData emptyData()
{
    MonitoringData data = {};
    return data;
}

} // namespace

TelemetryReporter::TelemetryReporter(ITelemetrySink &sink, const ReporterConfig &config)
    : sink_(sink),
      config_(config),
      last_publish_ms_(0UL),
      has_published_(false),
      published_count_(0UL),
      failed_count_(0UL)
{
    last_data_ = emptyData();
}

void TelemetryReporter::setConfig(const ReporterConfig &config)
{
    config_ = config;
}

const ReporterConfig &TelemetryReporter::config() const
{
    return config_;
}

void TelemetryReporter::reset()
{
    has_published_ = false;
    failed_count_ = 0UL;
    last_data_ = emptyData();
}

uint32_t TelemetryReporter::publishedCount() const
{
    return published_count_;
}

uint32_t TelemetryReporter::failedCount() const
{
    return failed_count_;
}

bool TelemetryReporter::hasPublished() const
{
    return has_published_;
}

uint32_t TelemetryReporter::lastPublishMs() const
{
    return last_publish_ms_;
}

bool TelemetryReporter::exceeds(float current, float previous, float delta)
{
    const float diff = (current > previous) ? (current - previous) : (previous - current);
    return diff >= delta;
}

bool TelemetryReporter::exceeds(int current, int previous, int delta)
{
    const int diff = (current > previous) ? (current - previous) : (previous - current);
    return diff >= delta;
}

void TelemetryReporter::update(uint32_t now_ms, const ControllerSnapshot &snapshot)
{
    MonitoringData data = emptyData();
    if (!toMonitoringData(snapshot, &data))
    {
        return;
    }

    if (!needsPublish(data, now_ms))
    {
        return;
    }

    tryPublish(data, now_ms);
}

bool TelemetryReporter::needsPublish(const MonitoringData &data, uint32_t now_ms) const
{
    // 首帧：立刻发，让云端拿到基线。
    if (!has_published_)
    {
        return true;
    }

    // 限流：距上次上报太近就压住（包括首帧之后的连续变化）。
    if (elapsed(now_ms, last_publish_ms_) < config_.min_interval_ms)
    {
        return false;
    }

    // 定期心跳
    if (elapsed(now_ms, last_publish_ms_) >= config_.heartbeat_ms)
    {
        return true;
    }

    // 环境量变化（MonitoringData 是 double，比较前显式收窄到 float 与阈值同精度）
    if (exceeds(static_cast<float>(data.temperature),
                static_cast<float>(last_data_.temperature), config_.temperature_delta) ||
        exceeds(static_cast<float>(data.air_humidity),
                static_cast<float>(last_data_.air_humidity), config_.air_humidity_delta) ||
        exceeds(static_cast<float>(data.soil_humidity),
                static_cast<float>(last_data_.soil_humidity), config_.soil_humidity_delta) ||
        exceeds(data.illuminance, last_data_.illuminance, config_.illuminance_delta))
    {
        return true;
    }

    // 执行器状态变化
    if (config_.report_on_actuator_change &&
        ((data.fan_on != last_data_.fan_on) || (data.light_on != last_data_.light_on) ||
         (data.heater_on != last_data_.heater_on)))
    {
        return true;
    }

    return false;
}

void TelemetryReporter::tryPublish(const MonitoringData &data, uint32_t now_ms)
{
    if (sink_.publish(data))
    {
        last_data_ = data;
        last_publish_ms_ = now_ms;
        has_published_ = true;
        published_count_++;
        return;
    }

    failed_count_++;
}

} // namespace greenhouse
