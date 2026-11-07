//
// 四路环境量的阈值判定框架：温度、空气湿度、土壤湿度、光照强度。
//
// 阈值来源：configuration/Configuration.h 的 GREENHOUSE_CONFIGURATION，
// 本文件不写死任何温度/湿度/光照数值。
//
// 防御：配置里的区间若自相矛盾（min >= max）或超出物理量程（如土壤湿度 25565 %），
// 该路回退到内置安全值，并置 usedFallbackLimits()，便于上层发现配置写错。
//
// 每个变量输出 UNKNOWN / LOW / NORMAL / HIGH / FAULT 五种状态，
// 并带回滞：进入越界用原始阈值，退出越界需回到阈值内侧的回滞带之外。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_VARIABLEJUDGE_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_VARIABLEJUDGE_H

#include <stdint.h>

#include "configuration/LogicalConfiguration.h"

namespace greenhouse {

enum class VarId : uint8_t
{
    TEMPERATURE = 0,
    AIR_HUMIDITY = 1,
    SOIL_HUMIDITY = 2,
    ILLUMINANCE = 3,
    COUNT = 4,
};

enum class VarState : uint8_t
{
    UNKNOWN = 0, // 还没拿到过有效读数
    LOW = 1,     // 低于下限
    NORMAL = 2,  // 在区间内
    HIGH = 3,    // 高于上限
    FAULT = 4,   // 本轮读数无效
};

// 一个变量的上下限与回滞带宽。
struct RangeLimits
{
    float low;
    float high;
    float hysteresis;
};

struct VariableReading
{
    bool valid; // false 表示本次采样失败
    float value;
};

struct VariableJudgement
{
    VarState state;
    float value; // 最近一次有效读数（FAULT 时保留旧值）
    RangeLimits limits;
};

// 一次完整采样的四路读数，字段名与 MonitoringData 一致。
struct EnvironmentSample
{
    VariableReading temperature;
    VariableReading air_humidity;
    VariableReading soil_humidity;
    VariableReading illuminance;
};

class VariableJudge
{
    public:
    // 四路上下限均取自 GREENHOUSE_CONFIGURATION（温度/空气湿度/土壤湿度/光照）。
    explicit VariableJudge(const LogicalStatics &config);

    // 单独覆盖某个变量的阈值（例如切换植物预设后调用）。
    void setLimits(VarId id, const RangeLimits &limits);
    RangeLimits limits(VarId id) const;

    // 判定单个变量，内部保存上一次状态用于回滞。
    VariableJudgement judge(VarId id, const VariableReading &reading);

    // 一次性判定四路。
    void judgeAll(const EnvironmentSample &sample);

    const VariableJudgement &judgement(VarId id) const;

    // bit0=温度 bit1=空气湿度 bit2=土壤湿度 bit3=光照；置位表示当前 LOW/HIGH。
    uint8_t outOfRangeMask() const;
    bool anyOutOfRange() const;

    // 是否因为配置不合法而使用了内置回退阈值（true 需检查 Configuration.h）。
    bool usedFallbackLimits() const;

    // 清空历史状态（例如重新上电或换盆后）。
    void reset();

    private:
    static uint8_t index(VarId id);
    static VarState nextState(VarState previous, const VariableReading &reading,
                              const RangeLimits &limits);

    // 从配置取值；不合法时用 fallback 并置 fallback_used_。
    void applyConfigLimits(VarId id, double low, double high, float hysteresis, float abs_low,
                           float abs_high, const RangeLimits &fallback);

    RangeLimits limits_[4];
    VariableJudgement judgements_[4];
    bool fallback_used_;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_VARIABLEJUDGE_H
