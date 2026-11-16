//
// Created by APairOfMoons on 2026/10/30.
//

#include "HardwareDataManager.h"

#include "aht20.h"
#include "common_data.h"
#include "hal_data.h"
#include "r_ioport.h"

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "motor.h"
#include "portmacro.h"
#include "projdefs.h"
#include "semphr.h"
#include "veml7700.h"

namespace {
constexpr TickType_t kWaitTimeoutTicks = pdMS_TO_TICKS(100);

StaticSemaphore_t g_i2c0_tx_sem_buffer;
StaticSemaphore_t g_i2c0_rx_sem_buffer;
StaticSemaphore_t g_i2c1_tx_sem_buffer;
StaticSemaphore_t g_i2c1_rx_sem_buffer;
StaticSemaphore_t g_adc0_scan_sem_buffer;

SemaphoreHandle_t g_i2c0_tx_sem = nullptr;
SemaphoreHandle_t g_i2c0_rx_sem = nullptr;
SemaphoreHandle_t g_i2c1_tx_sem = nullptr;
SemaphoreHandle_t g_i2c1_rx_sem = nullptr;
SemaphoreHandle_t g_adc0_scan_sem = nullptr;

void initSyncPrimitives() {
  if (g_i2c0_tx_sem == nullptr) {
    g_i2c0_tx_sem = xSemaphoreCreateBinaryStatic(&g_i2c0_tx_sem_buffer);
  }
  if (g_i2c0_rx_sem == nullptr) {
    g_i2c0_rx_sem = xSemaphoreCreateBinaryStatic(&g_i2c0_rx_sem_buffer);
  }
  if (g_i2c1_tx_sem == nullptr) {
    g_i2c1_tx_sem = xSemaphoreCreateBinaryStatic(&g_i2c1_tx_sem_buffer);
  }
  if (g_i2c1_rx_sem == nullptr) {
    g_i2c1_rx_sem = xSemaphoreCreateBinaryStatic(&g_i2c1_rx_sem_buffer);
  }
  if (g_adc0_scan_sem == nullptr) {
    g_adc0_scan_sem = xSemaphoreCreateBinaryStatic(&g_adc0_scan_sem_buffer);
  }
}
} // namespace

namespace HardwareDataManager {

void peripheral_init() {
  initSyncPrimitives();
  aht20::init();
  veml7700::init();
  R_ADC_Open(&g_adc0_ctrl, &g_adc0_cfg);
  R_ADC_ScanCfg(&g_adc0_ctrl, &g_adc0_channel_cfg);
  if (FSP_SUCCESS == R_ADC_ScanStart(&g_adc0_ctrl)) {
    ADC0WaitScanCplt();
  }
}

// 读取
bool readTemperatureHumidity(double *temperature, double *air_humidity) {
  if ((nullptr == temperature) || (nullptr == air_humidity)) {
    return false;
  }

  double temp = 0.0;
  double humi = 0.0;
  aht20::read(&temp, &humi); // 失败时写入 -999 / -1 哨兵

  if ((temp < -40.0) || (temp > 85.0) || (humi < 0.0) || (humi > 100.0)) {
    return false;
  }

  *temperature = temp;
  *air_humidity = humi;
  return true;
}

float readSoilHumidity() {
  if (FSP_SUCCESS == R_ADC_ScanStart(&g_adc0_ctrl)) {
    ADC0WaitScanCplt();
  }
  uint16_t data = 0;
  R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_1, &data);

  // 标定端点：干土 / 湿土的原始 ADC 读数（需实测替换）。
  constexpr float kAdcDry = 2510.0F;
  constexpr float kAdcWet = 920.0F;
  static_assert(kAdcDry > kAdcWet, "dry ADC value must be larger than wet");

  float percent = (kAdcDry - static_cast<float>(data)) /
                  (kAdcDry - kAdcWet) * 100.0F;
  if (percent < 0.0F) {
    percent = 0.0F;
  } else if (percent > 100.0F) {
    percent = 100.0F;
  }
  return percent;
}
double readIlluminance() { return veml7700::read(); }

bool readFanStatus() {
  bsp_io_level_t level = BSP_IO_LEVEL_LOW;
  const fsp_err_t err =
      R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_00_PIN_02, &level);
  if (FSP_SUCCESS != err) {
    return false;
  }
  return BSP_IO_LEVEL_HIGH == level;
}

bool readLightStatus() {
  bsp_io_level_t level = BSP_IO_LEVEL_LOW;
  const fsp_err_t err =
      R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_02, &level);
  if (FSP_SUCCESS != err) {
    return false;
  }
  return BSP_IO_LEVEL_HIGH == level;
}

bool readHeaterStatus() {
  if (0U == g_timer0_ctrl.open) {
    return false;
  }

  timer_status_t status{};
  const fsp_err_t err = R_GPT_StatusGet(&g_timer0_ctrl, &status);
  if (FSP_SUCCESS != err || TIMER_STATE_STOPPED == status.state) {
    return false;
  }

  return 2U != g_timer0_ctrl.p_reg->GTUDDTYC_b.OADTY;
}

bool readPumpStatus() {
  bsp_io_level_t level = BSP_IO_LEVEL_LOW;
  const fsp_err_t err =
      R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_07, &level);
  if (FSP_SUCCESS != err) {
    return false;
  }
  return BSP_IO_LEVEL_HIGH == level;
}

bool readWaterLevel() {
  bsp_io_level_t has_water;
  R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_06, &has_water);
  return has_water;
}

// 操作

bool setFanStatus(const bool status) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_00_PIN_02,
                    static_cast<bsp_io_level_t>(status));
  return true;
}
bool setLightStatus(const bool status) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_02,
                    static_cast<bsp_io_level_t>(status));
  return true;
}
bool setHeaterStatus(const uint8_t duty_cycle) {
  if (duty_cycle > 100U) {
    return false;
  }

  fsp_err_t err = FSP_SUCCESS;

  if (0U == g_timer0_ctrl.open) {
    err = R_GPT_Open(&g_timer0_ctrl, &g_timer0_cfg);
    if (FSP_SUCCESS != err) {
      return false;
    }
  }

  timer_info_t info{};
  err = R_GPT_InfoGet(&g_timer0_ctrl, &info);
  if (FSP_SUCCESS != err) {
    return false;
  }

  const auto duty_cycle_counts = static_cast<uint32_t>(
      (static_cast<uint64_t>(info.period_counts) * duty_cycle) / 100U);

  err =
      R_GPT_DutyCycleSet(&g_timer0_ctrl, duty_cycle_counts, GPT_IO_PIN_GTIOCA);
  if (FSP_SUCCESS != err) {
    return false;
  }

  timer_status_t status{};
  err = R_GPT_StatusGet(&g_timer0_ctrl, &status);
  if (FSP_SUCCESS != err) {
    return false;
  }

  if (TIMER_STATE_STOPPED == status.state) {
    err = R_GPT_Start(&g_timer0_ctrl);
    if (FSP_SUCCESS != err) {
      return false;
    }
  }

  return true;
}
bool setPumpStatus(const bool status) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_07,
                    static_cast<bsp_io_level_t>(status));
  return true;
}

bool rotateTurntable(const float deltaDegrees) {
  // 电机驱动只接受"角度大小 + 方向"，这里把带符号角度拆开：
  // 正角度右转（CW），负角度左转（CCW）。之前固定 CW 会把负角度当成
  // 负步数，循环一次都不执行。
  const float magnitude =
      (deltaDegrees < 0.0F) ? -deltaDegrees : deltaDegrees;
  const uint8_t direction =
      (deltaDegrees < 0.0F) ? MOTOR_DIRECTION_CCW : MOTOR_DIRECTION_CW;
  MotorDevGet()->rotate(magnitude, direction, 2);
  return true;
}
} // namespace HardwareDataManager

void i2c_callback(i2c_master_callback_args_t *p_args) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  switch (p_args->event) {
  case I2C_MASTER_EVENT_TX_COMPLETE: {
    if (g_i2c0_tx_sem != nullptr) {
      xSemaphoreGiveFromISR(g_i2c0_tx_sem, &xHigherPriorityTaskWoken);
    }
    break;
  }
  case I2C_MASTER_EVENT_RX_COMPLETE: {
    if (g_i2c0_rx_sem != nullptr) {
      xSemaphoreGiveFromISR(g_i2c0_rx_sem, &xHigherPriorityTaskWoken);
    }
    break;
  }
  default: {
    break;
  }
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void I2C0WaitTxCplt() {
  if (g_i2c0_tx_sem == nullptr) {
    initSyncPrimitives();
  }
  (void)xSemaphoreTake(g_i2c0_tx_sem, kWaitTimeoutTicks);
}

void I2C0WaitRxCplt() {
  if (g_i2c0_rx_sem == nullptr) {
    initSyncPrimitives();
  }
  (void)xSemaphoreTake(g_i2c0_rx_sem, kWaitTimeoutTicks);
}

void sci_i2c_master_callback(i2c_master_callback_args_t *p_args) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  switch (p_args->event) {
  case I2C_MASTER_EVENT_TX_COMPLETE: {
    if (g_i2c1_tx_sem != nullptr) {
      xSemaphoreGiveFromISR(g_i2c1_tx_sem, &xHigherPriorityTaskWoken);
    }
    break;
  }
  case I2C_MASTER_EVENT_RX_COMPLETE: {
    if (g_i2c1_rx_sem != nullptr) {
      xSemaphoreGiveFromISR(g_i2c1_rx_sem, &xHigherPriorityTaskWoken);
    }
    break;
  }
  default: {
    break;
  }
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void I2C1WaitTxCplt() {
  if (g_i2c1_tx_sem == nullptr) {
    initSyncPrimitives();
  }
  (void)xSemaphoreTake(g_i2c1_tx_sem, kWaitTimeoutTicks);
}

void I2C1WaitRxCplt() {
  if (g_i2c1_rx_sem == nullptr) {
    initSyncPrimitives();
  }
  (void)xSemaphoreTake(g_i2c1_rx_sem, kWaitTimeoutTicks);
}

void adc0_callback(adc_callback_args_t *p_args) {
  FSP_PARAMETER_NOT_USED(p_args);
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  if (g_adc0_scan_sem != nullptr) {
    xSemaphoreGiveFromISR(g_adc0_scan_sem, &xHigherPriorityTaskWoken);
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void ADC0WaitScanCplt() {
  if (g_adc0_scan_sem == nullptr) {
    initSyncPrimitives();
  }
  (void)xSemaphoreTake(g_adc0_scan_sem, kWaitTimeoutTicks);
}
