//
// Created by APairOfMoons on 2026/10/30.
//

#include "HardwareDataManager.h"

#include "common_data.h"
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
bool setLightStatus(bool status) {

  return false; // Failed attempt / No changes
}
bool setHeaterStatus(bool status) {

  return false; // Failed attempt / No changes
}
bool setPumpStatus(bool status) {

  return false; // Failed attempt / No changes
}

bool rotateTurntable(int deltaDegrees) {

  return false; // Rotate failed
}
} // namespace HardwareDataManager