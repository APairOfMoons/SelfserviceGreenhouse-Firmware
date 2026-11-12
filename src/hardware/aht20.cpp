//
// Created by Schwi on 2026/11/8.
//

#include "aht20.h"

#include "HardwareDataManager.h"
#include "hal_data.h"
#include "r_iic_master.h"

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "portmacro.h"
#include "projdefs.h"
#include "task.h"

constexpr uint8_t AHT20_CMD_INIT[3] = {0xBE, 0x08, 0x00};
constexpr uint8_t AHT20_CMD_MEASURE[3] = {0xAC, 0x33, 0x00};
constexpr uint8_t AHT20_CMD_RESET = 0xBA;
namespace aht20 {
uint8_t read_status() {
  uint8_t status = 0;
  const fsp_err_t err =
      R_IIC_MASTER_Read(&g_i2c_master0_ctrl, &status, 1, false);
  if (err != FSP_SUCCESS) {
    return 0xFF;
  }
  I2C0WaitRxCplt();
  return status;
}

void reset() {
  uint8_t cmd = AHT20_CMD_RESET;
  const fsp_err_t err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl, &cmd, 1, false);
  if (err == FSP_SUCCESS) {
    I2C0WaitTxCplt();
  }
  vTaskDelay(pdMS_TO_TICKS(20));
}

void init() {
  fsp_err_t err = R_IIC_MASTER_Open(&g_i2c_master0_ctrl, &g_i2c_master0_cfg);
  if (err != FSP_SUCCESS && err != FSP_ERR_ALREADY_OPEN) {
    return;
  }
  // 1. 上电等待至少 40ms
  vTaskDelay(pdMS_TO_TICKS(40));
  // 2. 读取状态字，检查校准使能位 Bit[3]
  const uint8_t status = read_status();
  if ((status & 0x08) == 0) {
    // 未校准，发送初始化命令（3 字节）
    err = R_IIC_MASTER_Write(&g_i2c_master0_ctrl,
                             const_cast<uint8_t *>(AHT20_CMD_INIT), 3, false);
    if (err == FSP_SUCCESS) {
      I2C0WaitTxCplt();
    }
    vTaskDelay(pdMS_TO_TICKS(10)); // 等待校准生效
  }
}

void measure() {
  // 发送触发测量命令（3 字节）
  const fsp_err_t err = R_IIC_MASTER_Write(
      &g_i2c_master0_ctrl, const_cast<uint8_t *>(AHT20_CMD_MEASURE), 3, false);
  if (err == FSP_SUCCESS) {
    I2C0WaitTxCplt();
  }
}

void read(double *temperature, double *humidity) {
  if (temperature == nullptr || humidity == nullptr) {
    return;
  }
  measure();
  uint8_t data[6] = {0};
  uint8_t retry_cnt = 0;
  while (true) {
    // 规范要求测量等待至少 75ms
    vTaskDelay(pdMS_TO_TICKS(75));
    // 直接读取 6
    // 个字节：Byte0为状态，Byte1~2为湿度，Byte3为湿度低4位+温度高4位，Byte4~5为温度
    const fsp_err_t err =
        R_IIC_MASTER_Read(&g_i2c_master0_ctrl, data, 6, false);
    if (err == FSP_SUCCESS) {
      I2C0WaitRxCplt();
    }
    // 检查 Bit[7]：0 为就绪，1 为忙碌中
    if ((data[0] & 0x80) == 0) {
      break;
    }
    retry_cnt++;
    if (retry_cnt >= 3) {
      *temperature = -999.0;
      *humidity = -1.0;
      return;
    }
  }
  // 解析 20 位湿度原始数据
  const uint32_t raw_humidity = (static_cast<uint32_t>(data[1]) << 12) |
                                (static_cast<uint32_t>(data[2]) << 4) |
                                (static_cast<uint32_t>(data[3]) >> 4);
  // 解析 20 位温度原始数据
  const uint32_t raw_temperature =
      ((static_cast<uint32_t>(data[3]) & 0x0F) << 16) |
      (static_cast<uint32_t>(data[4]) << 8) | static_cast<uint32_t>(data[5]);
  constexpr double resolution = 1048576.0; // 2^20
  *humidity = (static_cast<double>(raw_humidity) / resolution) * 100.0;
  *temperature =
      (static_cast<double>(raw_temperature) / resolution) * 200.0 - 50.0;
}
} // namespace aht20
