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
double readTemperature() {
  double temp, humi;
  aht20::read(&temp, &humi);
  return temp;
}
double readAirHumidity() {
  double temp, humi;
  aht20::read(&temp, &humi);
  return humi;
}
uint16_t readSoilHumidity() { // 2510 920
  if (FSP_SUCCESS == R_ADC_ScanStart(&g_adc0_ctrl)) {
    ADC0WaitScanCplt();
  }
  uint16_t data = 0;
  R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_1, &data);

  return data;
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

bool readWaterLevel() {}

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
  MotorDevGet()->rotate(deltaDegrees, MOTOR_DIRECTION_CW, 2);
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
