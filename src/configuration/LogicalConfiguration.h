//
// 逻辑配置：全部可调参数集中在这里。
//
// 分层：
//   VariableStatics —— 可下发、可修改的养护参数（网端通过 modifyConfiguration 提交）
//   FixedStatics    —— 固化的逻辑配置（不对外暴露下发入口）
//   LogicalStatics  —— 二者合体，是"当前生效配置"的完整快照
//
// 并发：通信线程提交、控制线程读取，统一用 FreeRTOS 临界区保护。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_LOGICALCONFIGURATION_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_LOGICALCONFIGURATION_H

#include <cstdint>

// ---------- 可变配置 ----------
struct VariableStatics
{
    int turnAnglePerDay;              // 每日累计受光角度

    // 温度（℃）
    double minTemperature;
    double maxTemperature;
    double temperatureHysteresis;     // 控温回滞带宽

    // 空气湿度（%）
    double minAirHumidity;
    double maxAirHumidity;
    double airHumidityHysteresis;

    // 土壤湿度（%）
    double minSoilHumidity;
    double maxSoilHumidity;

    // 光照（lx）
    double minIlluminance;
    double maxIlluminance;
    uint32_t illuminanceMinOnMs;      // 补光最短点亮时间

    // 昼夜窗口
    uint32_t dayStartMs;
    uint32_t dayLengthMs;

    // 浇水
    uint32_t pumpDurationMs;          // 单次浇水持续时间
    uint32_t pumpMinIntervalMs;       // 两次浇水最小间隔
    uint32_t pumpMaxPerDay;           // 每日浇水次数上限

    // 转盘
    int16_t maxTurnAngle;
    int16_t turnStepAngle;
    uint32_t turnIntervalMs;

    // 采样
    uint32_t samplePeriodMs;          // 数据采样周期
};

// ---------- 固化配置 ----------
struct FixedStatics
{
    uint8_t sensorFailLimit;          // 连续失败多少次判定传感器故障
    uint32_t manualTimeoutMs;         // 手动模式超时
    bool hasRtc;                      // 是否具备时间基准
};

// ---------- 完整逻辑配置 ----------
struct LogicalStatics
{
    VariableStatics variable;
    FixedStatics fixed;
};

extern LogicalStatics LOGICAL_CONFIGURATION;   // 定义在 .cpp，全局唯一

class LogicalConfiguration
{
    public:
    // 校验后提交可变配置；任一字段不合法则整份拒绝并返回 false，
    // 当前配置保持不变。
    static bool modifyConfiguration(const VariableStatics &candidate);

    // 取一份一致快照（临界区内整体拷贝）。
    static void snapshot(LogicalStatics *out);

    // 配置版本号：每次成功提交 +1，供控制层判断是否需要重新取快照。
    static uint32_t version();
};

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_LOGICALCONFIGURATION_H