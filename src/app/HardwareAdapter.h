//
// 硬件适配层：把 HardwareDataManager 的命名空间函数接到 IHardwareData。
//
// 之所以需要适配：
//   * HardwareDataManager 用 double/int 返回，并用魔数表示失败（温度 65535、
//     湿度/土壤/光照 -1）；
//   * IHardwareData 的约定是 bool 返回 + 出参，false 表示"本次读数无效"。
// 适配层负责把哨兵值翻译成 false，避免把 65535 ℃ 当成真实温度参与决策。
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREADAPTER_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_HARDWAREADAPTER_H

#include "logic/IHardwareData.h"

namespace greenhouse {

class HardwareAdapter : public IHardwareData
{
    public:
    HardwareAdapter() = default;

    bool readTemperature(float *out_temperature) override;
    bool readAirHumidity(float *out_air_humidity) override;
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