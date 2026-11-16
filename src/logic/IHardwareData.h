//
// 逻辑层与硬件的唯一接口。
//
// 命名与 hardware/HardwareDataManager.h 保持一致（readXxx / setXxxStatus / rotateTurntable），
// 读数语义与 communication/data/DataStructure.h 的 LogicData 保持一致
// （temperature / air_humidity / soil_humidity / illuminance / has_enough_water）。
//
// 本文件不含任何 FSP / FreeRTOS 头文件，可在 PC 上编译与测试。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_IHARDWAREDATA_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_IHARDWAREDATA_H

#include <stdint.h>

namespace greenhouse {

class IHardwareData
{
    public:
    virtual ~IHardwareData() = default;

    // ---------- 采样 ----------
    // 成功返回 true 并写入 *out；失败返回 false 且不修改 *out。
    // 实现方必须自带超时，禁止在驱动内部无限重试。
    //
    // 温湿度来自同一颗 AHT20：一次测量同时得到两个值，因此合并为一个接口，
    // 避免为了拿另一个值再触发一次 75 ms 的测量。false 表示两项都无效，
    // 控制器会把两路失败计数同时 +1（不允许只坏一项）。
    //
    // 注意：AHT20 上电后需要约 40 ms 才能通信，校准检查与初始化由硬件层
    // 在 peripheral_init() 里完成一次；本接口被调用前必须已初始化完毕。
    // 单次测量阻塞约 75~80 ms，采样周期（默认 1 s）需按此预留。
    virtual bool readTemperatureHumidity(float *out_temperature, float *out_air_humidity) = 0;

    // 土壤含水率，单位 %（0~100），不是 ADC 原始计数。
    virtual bool readSoilHumidity(float *out_soil_humidity) = 0;

    // 光照度，单位 lx。上限 65535，超过按饱和处理。
    virtual bool readIlluminance(uint16_t *out_illuminance) = 0;

    // 水位开关量：true = 水够用，false = 缺水。
    virtual bool readWaterLevel(bool *out_has_enough_water) = 0;

    // ---------- 执行器 ----------
    // 语义：同步接口，返回 true 表示动作【已确认完成】（调用会阻塞至完成或超时），
    //       false 表示超时 / 硬件拒动 / 限位冲突。
    // 约束（与实现方约定）：
    //   1) 必须有内部超时，禁止无限等待；
    //   2) 继电器类动作阻塞应 <= 20 ms，否则会拖慢控制循环的采样节拍；
    //   3) 长动作（转盘、水泵）若单次超过 50 ms，需另行提供非阻塞接口
    //      （start / isBusy / abort），不要靠阻塞主循环完成。
    virtual bool setFanStatus(bool status) = 0;
    virtual bool setLightStatus(bool status) = 0;

    // true = 满功率（100% 占空比），false = 关闭（0%）。
    // 硬件层当前用 GPT PWM 调功，逻辑层不做档位细分。
    virtual bool setHeaterStatus(bool status) = 0;

    virtual bool setPumpStatus(bool status) = 0;

    // 转盘相对当前位置转动 deltaDegrees（正 = 右，负 = 左），阻塞至动作结束。
    // 返回 false 表示超时 / 堵转 / 限位冲突。
    // 现实约束：步进电机约 4096 步/圈、2 ms/步，15° 约 0.34 s、60° 约 1.36 s，
    // 期间控制循环被占用；后续应改成非阻塞的 start / isBusy 接口。
    virtual bool rotateTurntable(int deltaDegrees) = 0;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_IHARDWAREDATA_H
