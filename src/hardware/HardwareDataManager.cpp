//
// Created by APairOfMoons on 2026/10/30.
//

#include "HardwareDataManager.h"

#include "common_data.h"
#include "hal_data.h"
#include "r_ioport.h"

namespace HardwareDataManager {
// 读取

double readTemperature() {

  return 65535; // Fail
}
double readAirHumidity() {

  return -1; // Fail
}
double readSoilHumidity() {

  return -1; // Fail
}
int readIlluminance() {

  return -1; // Fail
}

bool readFanStatus() {}
bool readLightStatus() {}
bool readHeaterStatus() {}
bool readPumpStatus() {}

bool readWaterLevel() {}

// 操作

bool setFanStatus(const bool status) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_00_PIN_01,
                    static_cast<bsp_io_level_t>(!status));
  return true;
}
bool setLightStatus(const bool status) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_02,
                    static_cast<bsp_io_level_t>(!status));
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

  err = R_GPT_DutyCycleSet(&g_timer0_ctrl, duty_cycle_counts,
                           GPT_IO_PIN_GTIOCA);
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
                    static_cast<bsp_io_level_t>(!status));
  return true;
}

bool rotateTurntable(int deltaDegrees) {

  return false; // Rotate failed
}
} // namespace HardwareDataManager