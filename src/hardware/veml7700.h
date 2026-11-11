//
// Created by Schwi on 2026/11/11.
//

#ifndef SELFSERVICEGREENHOUSE_FIRMWARE_VEML7700_H
#define SELFSERVICEGREENHOUSE_FIRMWARE_VEML7700_H

#include <cstdint>

namespace veml7700 {

// 1、ALS_GAIN 增益选择 (12:11)
enum AlsGain : uint16_t {
  ALS_GAIN_1 = static_cast<uint16_t>(0x0000),   // ALS gain x 1
  ALS_GAIN_2 = static_cast<uint16_t>(0x0001),   // ALS gain x 2
  ALS_GAIN_1_8 = static_cast<uint16_t>(0x0002), // ALS gain x (1/8)
  ALS_GAIN_1_4 = static_cast<uint16_t>(0x0003)  // ALS gain x (1/4)
};

// 2、ALS_IT 积分时间 (9:6)
enum AlsIt : uint16_t {
  ALS_IT_25ms = static_cast<uint16_t>(0x000C),  // 25 ms
  ALS_IT_50ms = static_cast<uint16_t>(0x0008),  // 50 ms
  ALS_IT_100ms = static_cast<uint16_t>(0x0000), // 100 ms
  ALS_IT_200ms = static_cast<uint16_t>(0x0001), // 200 ms
  ALS_IT_400ms = static_cast<uint16_t>(0x0002), // 400 ms
  ALS_IT_800ms = static_cast<uint16_t>(0x0003)  // 800 ms
};

// 3、ALS_PERS 中断持久保护次数 (5:4)
enum AlsPers : uint16_t {
  ALS_PERS_1 = static_cast<uint16_t>(0x0000), // 1
  ALS_PERS_2 = static_cast<uint16_t>(0x0001), // 2
  ALS_PERS_4 = static_cast<uint16_t>(0x0002), // 4
  ALS_PERS_8 = static_cast<uint16_t>(0x0003)  // 8
};

// 4、ALS_INT_EN 中断控制使能 (1)
enum AlsIntEn : uint16_t {
  ALS_INT_DIS = static_cast<uint16_t>(0x0000),  // 0: disable
  ALS_INT_EN_EN = static_cast<uint16_t>(0x0001) // 1: enable
};

// 5、ALS_SD 关断/开机控制 (0)
enum AlsSd : uint16_t {
  ALS_SD_ON = static_cast<uint16_t>(0x0000),  // 0: ALS power on
  ALS_SD_DOWN = static_cast<uint16_t>(0x0001) // 1: ALS shut down
};

/*------		省电寄存器		------*/
// 6、PSM 省电模式 (2:1)
enum PsmMode : uint16_t {
  PSM_M1 = static_cast<uint16_t>(0x0000), // mode 1
  PSM_M2 = static_cast<uint16_t>(0x0001), // mode 2
  PSM_M3 = static_cast<uint16_t>(0x0002), // mode 3
  PSM_M4 = static_cast<uint16_t>(0x0003)  // mode 4
};

// 7、PSM_EN 省电控制 (0)
enum PsmEn : uint16_t {
  PSM_EN_DIS = static_cast<uint16_t>(0x0000), // 0: disable
  PSM_EN_EN = static_cast<uint16_t>(0x0001)   // 1: enable
};

// 别名兼容
using VEML7700_ALS_GAIN = AlsGain;
using VEML7700_ALS_IT = AlsIt;
using VEML7700_ALS_PERS = AlsPers;
using VEML7700_ALS_INT_EN = AlsIntEn;
using VEML7700_ALS_SD = AlsSd;
using VEML7700_PSM = PsmMode;
using VEML7700_PSM_EN = PsmEn;

#pragma pack(push, 1)
/**
 * @brief VEML7700 配置寄存器 (Command Code #0, Register address = 00h)
 * 位域联合体
 */
union ConfigReg {
  uint16_t raw;
  struct {
    uint16_t als_sd : 1; // Bit 0     : ALS shut down setting (0: ALS power on,
                         // 1: ALS shut down)
    uint16_t als_int_en : 1; // Bit 1     : ALS interrupt enable setting (0: ALS
                             // INT disable, 1: ALS INT enable)
    uint16_t reserved1 : 2;  // Bit 3:2   : Set 00b
    uint16_t als_pers : 2; // Bit 5:4   : ALS persistence protect number setting
                           // (00: 1, 01: 2, 10: 4, 11: 8)
    uint16_t als_it : 4;   // Bit 9:6   : ALS integration time setting (1100:
                           // 25ms, 1000: 50ms, 0000: 100ms, ...)
    uint16_t reserved2 : 1; // Bit 10    : Set 0b
    uint16_t als_gain : 2;  // Bit 12:11 : Gain selection (00: x1, 01: x2, 10:
                            // x(1/8), 11: x(1/4))
    uint16_t reserved3 : 3; // Bit 15:13 : Set 000b
  } bits;
};

/**
 * @brief VEML7700 省电模式寄存器 (Command Code #3, Register address = 03h)
 * 位域联合体
 */
union PsmReg {
  uint16_t raw;
  struct {
    uint16_t psm_en : 1;    // Bit 0    : PSM enable
    uint16_t psm : 2;       // Bit 2:1  : Power saving mode (mode 1 ~ mode 4)
    uint16_t reserved : 13; // Bit 15:3 : Reserved
  } bits;
};
#pragma pack(pop)

using Veml7700ConfigReg = ConfigReg;
using Veml7700PsmReg = PsmReg;

// 驱动函数
void init();
double read();
void power_up(bool enable);

} // namespace veml7700

// 命名空间别名，支持 veml7700:: 和 VEML7700::
namespace VEML7700 = veml7700;

// 全局别名兼容
using Veml7700ConfigReg = veml7700::ConfigReg;
using Veml7700PsmReg = veml7700::PsmReg;

#endif // SELFSERVICEGREENHOUSE_FIRMWARE_VEML7700_H
