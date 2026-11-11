//
// Created by Schwi on 2026/11/11.
//

#include "veml7700.h"

#include "FreeRTOSConfig.h"
#include "HardwareDataManager.h"
#include "hal_data.h"
#include "FreeRTOS.h"
#include "portmacro.h"
#include "projdefs.h"
#include "r_sci_i2c.h"
#include "task.h"

static double get_resolution(const VEML7700_ALS_GAIN gain,
                             const VEML7700_ALS_IT it) {
  double gain_val = 1.0;
  switch (gain) {
  case ALS_GAIN_1:
    gain_val = 1.0;
    break;
  case ALS_GAIN_2:
    gain_val = 2.0;
    break;
  case ALS_GAIN_1_8:
    gain_val = 0.125;
    break;
  case ALS_GAIN_1_4:
    gain_val = 0.25;
    break;
  }
  double it_val_ms = 100.0;
  switch (it) {
  case ALS_IT_25ms:
    it_val_ms = 25.0;
    break;
  case ALS_IT_50ms:
    it_val_ms = 50.0;
    break;
  case ALS_IT_100ms:
    it_val_ms = 100.0;
    break;
  case ALS_IT_200ms:
    it_val_ms = 200.0;
    break;
  case ALS_IT_400ms:
    it_val_ms = 400.0;
    break;
  case ALS_IT_800ms:
    it_val_ms = 800.0;
    break;
  }
  // 根据 Vishay 应用手册 (Doc #84323, Page 5):
  // 最高灵敏度基准为 Gain = 2, IT = 800ms 时，典型分辨率为 0.0042 lx/cnt
  // (等价于标准模式 Gain = 1, IT = 100ms 时的 0.0672 lx/cnt)
  return 0.0042 * (2.0 / gain_val) * (800.0 / it_val_ms);
}

static void init() {
  const fsp_err_t err = R_SCI_I2C_Open(&g_i2c0_ctrl, &g_i2c0_cfg);
  if (err != FSP_SUCCESS && err != FSP_ERR_ALREADY_OPEN) {
    return;
  }

  vTaskDelay(pdMS_TO_TICKS(5));

  constexpr Veml7700ConfigReg cfg_reg = {.bits = {.als_sd = ALS_SD_ON,
                                                  .als_int_en = ALS_INT_DIS,
                                                  .reserved1 = 0,
                                                  .als_pers = ALS_PERS_1,
                                                  .als_it = ALS_IT_400ms,
                                                  .reserved2 = 0,
                                                  .als_gain = ALS_GAIN_1_8,
                                                  .reserved3 = 0}};

  constexpr Veml7700PsmReg psm_reg = {
      .bits = {.psm_en = PSM_EN_EN, .psm = PSM_M3, .reserved = 0}};

  uint8_t buffer[3];
  buffer[0] = 0x00;
  buffer[1] = static_cast<uint8_t>(cfg_reg.raw & 0xFF);        // LSB
  buffer[2] = static_cast<uint8_t>((cfg_reg.raw >> 8) & 0xFF); // MSB
  R_SCI_I2C_Write(&g_i2c0_ctrl, &buffer[0], 3, false);
  I2C1WaitTxCplt();

  buffer[0] = 0x03;
  buffer[1] = static_cast<uint8_t>(psm_reg.raw & 0xFF);        // LSB
  buffer[2] = static_cast<uint8_t>((psm_reg.raw >> 8) & 0xFF); // MSB
  R_SCI_I2C_Write(&g_i2c0_ctrl, &buffer[0], 3, false);
  I2C1WaitTxCplt();
}

static double read() {
  uint8_t buffer[2];
  buffer[0] = 0x04;
  R_SCI_I2C_Write(&g_i2c0_ctrl, &buffer[0], 1, true);
  I2C1WaitTxCplt();
  R_SCI_I2C_Read(&g_i2c0_ctrl, &buffer[0], 2, false);
  I2C1WaitRxCplt();

  const uint16_t raw_als =
      static_cast<uint16_t>(buffer[0] | (static_cast<uint16_t>(buffer[1]) << 8));

  const double resolution = get_resolution(ALS_GAIN_1_8, ALS_IT_400ms);
  double lux_val = static_cast<double>(raw_als) * resolution;

  if (lux_val > 1000.0) {
    const double l = lux_val;
    lux_val = (6.0135e-13 * std::pow(l, 4)) - (9.3924e-9 * std::pow(l, 3)) +
              (8.1488e-5 * std::pow(l, 2)) + (1.0023 * l);
  }
  return  lux_val;
}

static Veml7700Dev g_veml7700_dev = {
    .name = "VEML7700", .read = read, .init = init};

Veml7700Dev *Veml7700DevGet() { return &g_veml7700_dev; }
