//
// Created by Schwi on 2026/11/9.
//

#include "motor.h"

#include "common_data.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "portmacro.h"
#include "projdefs.h"
#include "task.h"

constexpr uint8_t STEP_SEQ[8][4] = {{1, 0, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0},
                                    {0, 1, 1, 0}, {0, 0, 1, 0}, {0, 0, 1, 1},
                                    {0, 0, 0, 1}, {1, 0, 0, 1}};

static void set_pins(uint8_t in1, uint8_t in2, uint8_t in3, uint8_t in4) {
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_01_PIN_03,
                    static_cast<bsp_io_level_t>(in1));
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_01_PIN_04,
                    static_cast<bsp_io_level_t>(in2));
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_01_PIN_10,
                    static_cast<bsp_io_level_t>(in3));
  R_IOPORT_PinWrite(&g_ioport_ctrl, BSP_IO_PORT_04_PIN_09,
                    static_cast<bsp_io_level_t>(in4));
}

static void stop() { set_pins(0, 0, 0, 0); }

static void rotate(const float angle, const uint8_t direction,
                   const uint16_t delay_ms) {
  const auto steps = static_cast<uint32_t>(angle / 360.0f * 4096.0f);
  static uint8_t current_step = 0;
  for (uint32_t i = 0; i < steps; i++) {
    if (direction) {
      current_step = (current_step + 1) % 8;
    } else {
      current_step = (current_step + 7) % 8; // 相当于 -1 后取模
    }
    set_pins(STEP_SEQ[current_step][0], STEP_SEQ[current_step][1],
             STEP_SEQ[current_step][2], STEP_SEQ[current_step][3]);
    vTaskDelay(pdMS_TO_TICKS(delay_ms)); // 每拍延时
  }
  stop();
}

static MotorDev g_motor_dev = {
  .name = "28BYJ-48",
  .rotate = rotate,
  .stop = stop
};

MotorDev *MotorDevGet() { return &g_motor_dev; }
