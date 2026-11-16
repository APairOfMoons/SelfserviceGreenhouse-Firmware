//
// 硬件适配层：把 HardwareDataManager 的命名空间函数接到 IHardwareData。
//
// 之所以需要适配：
//   * HardwareDataManager 用 double 返回，并用哨兵值表示失败（温度 -999、
//     湿度 -1）；
//   * IHardwareData 的约定是 bool 返回 + 出参，false 表示"本次读数无效"。
// 适配层负责把哨兵值翻译成 false，避免把 -999 ℃ 当成真实温度参与决策。
//
// 另外三处语义映射：
//   * 加热：逻辑层布尔 → 100% / 0% PWM 占空比；
//   * 转盘：逻辑层 int 角度 → 硬件 float 角度（硬件负责按符号选方向）；
//   * 光照：double lx → uint16，超过 65535 饱和。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREADAPTER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREADAPTER_H

#include "logic/IHardwareData.h"

namespace greenhouse {

class HardwareAdapter : public IHardwareData
{
    public:
    HardwareAdapter() = default;

    bool readTemperatureHumidity(float *out_temperature,
                                 float *out_air_humidity) override;
    bool readSoilHumidity(float *out_soil_humidity) override;
    bool readIlluminance(uint16_t *out_illuminance) override;
    bool readWaterLevel(bool *out_has_enough_water) override;

    bool setFanStatus(bool status) override;
    bool setLightStatus(bool status) override;
    bool setHeaterStatus(bool status) override;
    bool setPumpStatus(bool status) override;

    bool rotateTurntable(int deltaDegrees) override;
};

} // namespace greenhouse

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREADAPTER_H
