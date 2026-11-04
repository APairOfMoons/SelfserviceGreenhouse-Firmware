//
// 四路环境量阈值判定实现。
// 温度、空气湿度、土壤湿度、光照的上下限均引用 GREENHOUSE_CONFIGURATION。
//

#include "logic/VariableJudge.h"

namespace greenhouse {
namespace {

// 回滞带宽：属于判定策略，不是植物养护参数，配置里没有这一项。
constexpr float kTemperatureHysteresis = 0.5F;   // ℃
constexpr float kAirHumidityHysteresis = 2.0F;   // %RH
constexpr float kSoilHumidityHysteresis = 3.0F;  // %
constexpr float kIlluminanceHysteresis = 200.0F; // lx

// 配置不合法时的回退值（与 Configuration.h 的默认值同量级）。
constexpr RangeLimits kTemperatureFallback = {10.0F, 30.0F, kTemperatureHysteresis};
constexpr RangeLimits kAirHumidityFallback = {30.0F, 90.0F, kAirHumidityHysteresis};
constexpr RangeLimits kSoilHumidityFallback = {30.0F, 55.0F, kSoilHumidityHysteresis};
constexpr RangeLimits kIlluminanceFallback = {1200.0F, 100000.0F, kIlluminanceHysteresis};

// 物理量程，用于识别配置里的单位/字段错位（例如 25565 落在湿度上）。
constexpr float kTemperatureAbsMin = -20.0F;
constexpr float kTemperatureAbsMax = 80.0F;
constexpr float kHumidityAbsMin = 0.0F;
constexpr float kHumidityAbsMax = 100.0F;
constexpr float kIlluminanceAbsMin = 0.0F;
constexpr float kIlluminanceAbsMax = 200000.0F;

} // namespace

VariableJudge::VariableJudge(const GreenhouseConfiguration &config)
    : fallback_used_(false)
{
    applyConfigLimits(VarId::TEMPERATURE, config.MIN_TEMPERATURE, config.MAX_TEMPERATURE,
                      kTemperatureHysteresis, kTemperatureAbsMin, kTemperatureAbsMax,
                      kTemperatureFallback);

    applyConfigLimits(VarId::AIR_HUMIDITY, config.MIN_AIR_HUMIDITY, config.MAX_AIR_HUMIDITY,
                      kAirHumidityHysteresis, kHumidityAbsMin, kHumidityAbsMax,
                      kAirHumidityFallback);

    applyConfigLimits(VarId::SOIL_HUMIDITY, config.MIN_SOIL_HUMIDITY, config.MAX_SOIL_HUMIDITY,
                      kSoilHumidityHysteresis, kHumidityAbsMin, kHumidityAbsMax,
                      kSoilHumidityFallback);

    applyConfigLimits(VarId::ILLUMINANCE, config.MIN_ILLUMINANCE, config.MAX_ILLUMINANCE,
                      kIlluminanceHysteresis, kIlluminanceAbsMin, kIlluminanceAbsMax,
                      kIlluminanceFallback);

    reset();
}

void VariableJudge::applyConfigLimits(VarId id, double low, double high, float hysteresis,
                                      float abs_low, float abs_high,
                                      const RangeLimits &fallback)
{
    const float low_f = static_cast<float>(low);
    const float high_f = static_cast<float>(high);

    const bool in_range = (low_f >= abs_low) && (high_f <= abs_high);
    const bool ordered = (low_f < high_f);
    const bool sane_hysteresis = (hysteresis > 0.0F) && (hysteresis < (high_f - low_f));

    if (in_range && ordered && sane_hysteresis)
    {
        limits_[index(id)] = {low_f, high_f, hysteresis};
        return;
    }

    limits_[index(id)] = fallback;
    fallback_used_ = true;
}

void VariableJudge::reset()
{
    for (uint8_t i = 0U; i < static_cast<uint8_t>(VarId::COUNT); ++i)
    {
        judgements_[i].state = VarState::UNKNOWN;
        judgements_[i].value = 0.0F;
        judgements_[i].limits = limits_[i];
    }
}

void VariableJudge::setLimits(VarId id, const RangeLimits &limits)
{
    limits_[index(id)] = limits;
    judgements_[index(id)].limits = limits;
}

RangeLimits VariableJudge::limits(VarId id) const
{
    return limits_[index(id)];
}

uint8_t VariableJudge::index(VarId id)
{
    const uint8_t raw = static_cast<uint8_t>(id);
    return (raw < static_cast<uint8_t>(VarId::COUNT)) ? raw : 0U;
}

VarState VariableJudge::nextState(VarState previous, const VariableReading &reading,
                                  const RangeLimits &limits)
{
    if (!reading.valid)
    {
        return VarState::FAULT;
    }

    // 进入越界：用原始阈值判定。
    if (reading.value < limits.low)
    {
        return VarState::LOW;
    }
    if (reading.value > limits.high)
    {
        return VarState::HIGH;
    }

    // 退出越界：必须越过回滞带，避免边界抖动。
    if ((VarState::LOW == previous) && (reading.value < (limits.low + limits.hysteresis)))
    {
        return VarState::LOW;
    }
    if ((VarState::HIGH == previous) && (reading.value > (limits.high - limits.hysteresis)))
    {
        return VarState::HIGH;
    }

    return VarState::NORMAL;
}

VariableJudgement VariableJudge::judge(VarId id, const VariableReading &reading)
{
    const uint8_t idx = index(id);
    VariableJudgement &slot = judgements_[idx];

    const VarState state = nextState(slot.state, reading, slot.limits);

    if (reading.valid)
    {
        slot.value = reading.value;
    }
    slot.state = state;

    return slot;
}

void VariableJudge::judgeAll(const EnvironmentSample &sample)
{
    (void) judge(VarId::TEMPERATURE, sample.temperature);
    (void) judge(VarId::AIR_HUMIDITY, sample.air_humidity);
    (void) judge(VarId::SOIL_HUMIDITY, sample.soil_humidity);
    (void) judge(VarId::ILLUMINANCE, sample.illuminance);
}

const VariableJudgement &VariableJudge::judgement(VarId id) const
{
    return judgements_[index(id)];
}

uint8_t VariableJudge::outOfRangeMask() const
{
    uint8_t mask = 0U;
    for (uint8_t i = 0U; i < static_cast<uint8_t>(VarId::COUNT); ++i)
    {
        if ((VarState::LOW == judgements_[i].state) || (VarState::HIGH == judgements_[i].state))
        {
            mask = static_cast<uint8_t>(mask | (1U << i));
        }
    }
    return mask;
}

bool VariableJudge::anyOutOfRange() const
{
    return 0U != outOfRangeMask();
}

bool VariableJudge::usedFallbackLimits() const
{
    return fallback_used_;
}

} // namespace greenhouse
