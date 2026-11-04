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
    virtual bool readTemperature(float *out_temperature) = 0;
    virtual bool readAirHumidity(float *out_air_humidity) = 0;
    virtual bool readSoilHumidity(float *out_soil_humidity) = 0;
    virtual bool readIlluminance(uint16_t *out_illuminance) = 0;
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
    virtual bool setHeaterStatus(bool status) = 0;
    virtual bool setPumpStatus(bool status) = 0;

    // 转盘相对当前位置转动 deltaDegrees（正 = 右，负 = 左），阻塞至动作结束。
    // 返回 false 表示超时 / 堵转 / 限位冲突。
    virtual bool rotateTurntable(int deltaDegrees) = 0;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_IHARDWAREDATA_H
