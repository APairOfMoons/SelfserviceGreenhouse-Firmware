//
// Created by APairOfMoons on 2026/10/30.
//

#include "HardwareDataManager.h"
#include <cmath>

#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "aht20.h"
#include "common_data.h"
#include "hal_data.h"
#include "motor.h"
#include "portmacro.h"
#include "projdefs.h"
#include "r_ioport.h"
#include "task.h"
#include "veml7700.h"

volatile bool gI2C0TxCplt = false;
volatile bool gI2C0RxCplt = false;

volatile bool gI2C1TxCplt = false;
volatile bool gI2C1RxCplt = false;

namespace HardwareDataManager {

void peripheral_init() {
  aht20::init();
  veml7700::init();
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
double readSoilHumidity() {

  return -1; // Fail
}
double readIlluminance() {
  return veml7700::read();
}

bool readFanStatus() {
  bsp_io_level_t level = BSP_IO_LEVEL_LOW;
  const fsp_err_t err =
      R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_00_PIN_01, &level);
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
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_00_PIN_01,
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
  switch (p_args->event) {
  case I2C_MASTER_EVENT_TX_COMPLETE: {
    gI2C0TxCplt = true;
    break;
  }
  case I2C_MASTER_EVENT_RX_COMPLETE: {
    gI2C0RxCplt = true;
    break;
  }
  default: {
    gI2C0TxCplt = gI2C0RxCplt = false;
    break;
  }
  }
}

void I2C0WaitTxCplt() {
  uint16_t wTimeOut = 100;
  while (!gI2C0TxCplt && wTimeOut) {
    vTaskDelay(pdMS_TO_TICKS(1));
    wTimeOut--;
  }
  gI2C0TxCplt = false;
}

void I2C0WaitRxCplt() {
  uint16_t wTimeOut = 100;
  while (!gI2C0RxCplt && wTimeOut) {
    vTaskDelay(pdMS_TO_TICKS(1));
    wTimeOut--;
  }
  gI2C0RxCplt = false;
}

void sci_i2c_master_callback(i2c_master_callback_args_t *p_args) {
  switch (p_args->event) {
  case I2C_MASTER_EVENT_TX_COMPLETE: {
    gI2C1TxCplt = true;
    break;
  }
  case I2C_MASTER_EVENT_RX_COMPLETE: {
    gI2C1RxCplt = true;
    break;
  }
  default: {
    gI2C1TxCplt = gI2C1RxCplt = false;
    break;
  }
  }
}

void I2C1WaitTxCplt() {
  uint16_t wTimeOut = 100;
  while (!gI2C1TxCplt && wTimeOut) {
    vTaskDelay(pdMS_TO_TICKS(1));
    wTimeOut--;
  }
  gI2C1TxCplt = false;
}

void I2C1WaitRxCplt() {
  uint16_t wTimeOut = 100;
  while (!gI2C1RxCplt && wTimeOut) {
    vTaskDelay(pdMS_TO_TICKS(1));
    wTimeOut--;
  }
  gI2C1RxCplt = false;
}
