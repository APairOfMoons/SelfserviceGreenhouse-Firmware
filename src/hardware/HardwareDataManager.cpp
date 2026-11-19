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

#include <cmath>

namespace {
constexpr TickType_t kWaitTimeoutTicks = pdMS_TO_TICKS(100);
constexpr float kInvalidSoilHumidity = -1.0F;
constexpr double kInvalidIlluminance = -1.0;

struct I2cWaitState {
  volatile i2c_master_event_t last_tx_event;
  volatile i2c_master_event_t last_rx_event;
  uint32_t tx_wait_calls;
  uint32_t tx_ok_calls;
  uint32_t rx_wait_calls;
  uint32_t rx_ok_calls;
  bool last_rx_ok;
  bool aborted_or_timed_out;
};

struct I2cBusContext {
  const i2c_master_instance_t *instance;
  SemaphoreHandle_t *tx_sem;
  SemaphoreHandle_t *rx_sem;
  I2cWaitState *state;
};

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

I2cWaitState g_i2c0_state = {};
I2cWaitState g_i2c1_state = {};
volatile bool g_adc0_scan_complete = false;
bool g_adc0_wait_ok = false;
bool g_adc0_ready = false;
bool g_veml7700_ready = false;

constexpr I2cBusContext kI2c0Bus = {
    &g_i2c_master0, &g_i2c0_tx_sem, &g_i2c0_rx_sem, &g_i2c0_state};
constexpr I2cBusContext kI2c1Bus = {
    &g_i2c0, &g_i2c1_tx_sem, &g_i2c1_rx_sem, &g_i2c1_state};

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

void drainSemaphore(SemaphoreHandle_t sem) {
  if (nullptr != sem) {
    (void)xSemaphoreTake(sem, 0);
  }
}

void resetI2cWaitState(I2cWaitState *state) {
  if (nullptr == state) {
    return;
  }
  state->last_tx_event = I2C_MASTER_EVENT_ABORTED;
  state->last_rx_event = I2C_MASTER_EVENT_ABORTED;
  state->tx_wait_calls = 0U;
  state->tx_ok_calls = 0U;
  state->rx_wait_calls = 0U;
  state->rx_ok_calls = 0U;
  state->last_rx_ok = false;
  state->aborted_or_timed_out = false;
}

bool isI2cBusOpen(const i2c_master_instance_t *instance) {
  if ((nullptr == instance) || (nullptr == instance->p_api) ||
      (nullptr == instance->p_api->statusGet) || (nullptr == instance->p_ctrl)) {
    return false;
  }
  i2c_master_status_t bus_status{};
  return (FSP_SUCCESS ==
          instance->p_api->statusGet(instance->p_ctrl, &bus_status)) &&
         bus_status.open;
}

void prepareI2cBus(const I2cBusContext &bus) {
  initSyncPrimitives();
  drainSemaphore(*bus.tx_sem);
  drainSemaphore(*bus.rx_sem);
  resetI2cWaitState(bus.state);
}

void handleI2cCallback(const i2c_master_callback_args_t *p_args,
                       const bool is_read_dir, const I2cBusContext &bus) {
  if (nullptr == p_args) {
    return;
  }
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  switch (p_args->event) {
  case I2C_MASTER_EVENT_TX_COMPLETE: {
    bus.state->last_tx_event = I2C_MASTER_EVENT_TX_COMPLETE;
    if (nullptr != *bus.tx_sem) {
      xSemaphoreGiveFromISR(*bus.tx_sem, &xHigherPriorityTaskWoken);
    }
    break;
  }
  case I2C_MASTER_EVENT_RX_COMPLETE: {
    bus.state->last_rx_event = I2C_MASTER_EVENT_RX_COMPLETE;
    if (nullptr != *bus.rx_sem) {
      xSemaphoreGiveFromISR(*bus.rx_sem, &xHigherPriorityTaskWoken);
    }
    break;
  }
  case I2C_MASTER_EVENT_ABORTED: {
    if (is_read_dir) {
      bus.state->last_rx_event = I2C_MASTER_EVENT_ABORTED;
      if (nullptr != *bus.rx_sem) {
        xSemaphoreGiveFromISR(*bus.rx_sem, &xHigherPriorityTaskWoken);
      }
    } else {
      bus.state->last_tx_event = I2C_MASTER_EVENT_ABORTED;
      if (nullptr != *bus.tx_sem) {
        xSemaphoreGiveFromISR(*bus.tx_sem, &xHigherPriorityTaskWoken);
      }
    }
    break;
  }
  default: {
    break;
  }
  }
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void waitI2cCompletion(const I2cBusContext &bus, const bool is_rx) {
  initSyncPrimitives();
  SemaphoreHandle_t sem = is_rx ? *bus.rx_sem : *bus.tx_sem;
  if (is_rx) {
    bus.state->rx_wait_calls++;
    bus.state->last_rx_ok = false;
  } else {
    bus.state->tx_wait_calls++;
  }

  if (pdTRUE == xSemaphoreTake(sem, kWaitTimeoutTicks)) {
    const i2c_master_event_t expected =
        is_rx ? I2C_MASTER_EVENT_RX_COMPLETE : I2C_MASTER_EVENT_TX_COMPLETE;
    const i2c_master_event_t actual =
        is_rx ? bus.state->last_rx_event : bus.state->last_tx_event;
    if (expected == actual) {
      if (is_rx) {
        bus.state->rx_ok_calls++;
        bus.state->last_rx_ok = true;
      } else {
        bus.state->tx_ok_calls++;
      }
    } else {
      bus.state->aborted_or_timed_out = true;
    }
    return;
  }

  bus.state->aborted_or_timed_out = true;
  if (isI2cBusOpen(bus.instance) && (nullptr != bus.instance->p_api->abort)) {
    (void)bus.instance->p_api->abort(bus.instance->p_ctrl);
  }
  drainSemaphore(sem);
}

bool readOutputPin(const bsp_io_port_pin_t pin) {
  bsp_io_level_t level = BSP_IO_LEVEL_LOW;
  if (FSP_SUCCESS != R_IOPORT_PinRead(&g_ioport_ctrl, pin, &level)) {
    return false;
  }
  return BSP_IO_LEVEL_HIGH == level;
}

bool writeOutputPin(const bsp_io_port_pin_t pin, const bool high) {
  const bsp_io_level_t level = high ? BSP_IO_LEVEL_HIGH : BSP_IO_LEVEL_LOW;
  return FSP_SUCCESS == R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
}

bool initVeml7700() {
  g_veml7700_ready = false;
  prepareI2cBus(kI2c1Bus);

  veml7700::init();

  if (!isI2cBusOpen(kI2c1Bus.instance)) {
    return false;
  }

  // veml7700::init() 依次写 0x00（退出关断态）与 0x03（PSM），两次都成功才视为就绪。
  if ((2U == g_i2c1_state.tx_wait_calls) && (2U == g_i2c1_state.tx_ok_calls) &&
      !g_i2c1_state.aborted_or_timed_out) {
    g_veml7700_ready = true;
    return true;
  }

  return false;
}
} // namespace

namespace HardwareDataManager {

void peripheral_init() {
  initSyncPrimitives();

  // 进入传感器延时初始化前先确保所有执行器处于安全关闭态。
  (void)setFanStatus(false);
  (void)setLightStatus(false);
  (void)setPumpStatus(false);
  (void)setHeaterStatus(0U);
  const MotorDev *const motor = MotorDevGet();
  if (nullptr != motor && nullptr != motor->stop) {
    motor->stop();
  }

  prepareI2cBus(kI2c0Bus);
  aht20::init();

  (void)initVeml7700();

  g_adc0_ready = false;
  fsp_err_t adc_err = R_ADC_Open(&g_adc0_ctrl, &g_adc0_cfg);
  if ((FSP_SUCCESS == adc_err) || (FSP_ERR_ALREADY_OPEN == adc_err)) {
    adc_err = R_ADC_ScanCfg(&g_adc0_ctrl, &g_adc0_channel_cfg);
    if (FSP_SUCCESS == adc_err) {
      g_adc0_ready = true;
      drainSemaphore(g_adc0_scan_sem);
      g_adc0_scan_complete = false;
      if (FSP_SUCCESS == R_ADC_ScanStart(&g_adc0_ctrl)) {
        ADC0WaitScanCplt();
      }
    }
  }
}

// 读取
bool readTemperatureHumidity(double *temperature, double *air_humidity) {
  if ((nullptr == temperature) || (nullptr == air_humidity)) {
    return false;
  }

  if (!isI2cBusOpen(kI2c0Bus.instance)) {
    return false;
  }

  prepareI2cBus(kI2c0Bus);

  double temp = 0.0;
  double humi = 0.0;
  aht20::read(&temp, &humi); // 失败时写入 -999 / -1 哨兵

  if (1U != g_i2c0_state.tx_wait_calls || 1U != g_i2c0_state.tx_ok_calls ||
      (g_i2c0_state.rx_wait_calls < 1U) || !g_i2c0_state.last_rx_ok ||
      g_i2c0_state.aborted_or_timed_out) {
    return false;
  }

  if (!std::isfinite(temp) || !std::isfinite(humi) || (temp < -40.0) ||
      temp > 85.0 || humi < 0.0 || humi > 100.0) {
    return false;
  }

  *temperature = temp;
  *air_humidity = humi;
  return true;
}

float readSoilHumidity() {
  if (!g_adc0_ready || (nullptr == g_adc0_ctrl.p_reg)) {
    return kInvalidSoilHumidity;
  }

  initSyncPrimitives();

  adc_status_t adc_status{};
  if ((FSP_SUCCESS != R_ADC_StatusGet(&g_adc0_ctrl, &adc_status)) ||
      (ADC_STATE_IDLE != adc_status.state)) {
    return kInvalidSoilHumidity;
  }

  drainSemaphore(g_adc0_scan_sem);
  g_adc0_scan_complete = false;
  g_adc0_wait_ok = false;

  if (FSP_SUCCESS != R_ADC_ScanStart(&g_adc0_ctrl)) {
    return kInvalidSoilHumidity;
  }

  ADC0WaitScanCplt();
  if (!g_adc0_wait_ok) {
    return kInvalidSoilHumidity;
  }

  uint16_t data = 0U;
  if (FSP_SUCCESS != R_ADC_Read(&g_adc0_ctrl, ADC_CHANNEL_1, &data)) {
    return kInvalidSoilHumidity;
  }
  if (data > 4095U) {
    return kInvalidSoilHumidity;
  }

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

double readIlluminance() {
  initSyncPrimitives();

  if (!g_veml7700_ready && !initVeml7700()) {
    return kInvalidIlluminance;
  }

  if (!isI2cBusOpen(kI2c1Bus.instance)) {
    return kInvalidIlluminance;
  }

  prepareI2cBus(kI2c1Bus);

  const double lux = veml7700::read();

  if ((1U != g_i2c1_state.tx_wait_calls) || (1U != g_i2c1_state.tx_ok_calls) ||
      (1U != g_i2c1_state.rx_wait_calls) || (1U != g_i2c1_state.rx_ok_calls) ||
      g_i2c1_state.aborted_or_timed_out) {
    return kInvalidIlluminance;
  }

  if (!std::isfinite(lux) || (lux < 0.0)) {
    return kInvalidIlluminance;
  }

  return lux;
}

bool readFanStatus() { return readOutputPin(BSP_IO_PORT_00_PIN_02); }

bool readLightStatus() { return readOutputPin(BSP_IO_PORT_04_PIN_02); }

bool readHeaterStatus() {
  if ((0U == g_timer0_ctrl.open) || (nullptr == g_timer0_ctrl.p_reg)) {
    return false;
  }

  timer_status_t status{};
  const fsp_err_t err = R_GPT_StatusGet(&g_timer0_ctrl, &status);
  if ((FSP_SUCCESS != err) || (TIMER_STATE_STOPPED == status.state)) {
    return false;
  }

  return 2U != g_timer0_ctrl.p_reg->GTUDDTYC_b.OADTY;
}

bool readPumpStatus() { return readOutputPin(BSP_IO_PORT_02_PIN_07); }

bool readWaterLevel(bool *has_water) {
  if (nullptr == has_water) {
    return false;
  }

  bsp_io_level_t level = BSP_IO_LEVEL_LOW;
  const fsp_err_t err =
      R_IOPORT_PinRead(&g_ioport_ctrl, BSP_IO_PORT_02_PIN_06, &level);
  if (FSP_SUCCESS != err) {
    return false;
  }

  *has_water = (BSP_IO_LEVEL_HIGH == level);
  return true;
}

bool readWaterLevel() {
  bool has_water = false;
  if (!readWaterLevel(&has_water)) {
    return false;
  }
  return has_water;
}

// 操作

bool setFanStatus(const bool status) {
  return writeOutputPin(BSP_IO_PORT_00_PIN_02, status);
}

bool setLightStatus(const bool status) {
  return writeOutputPin(BSP_IO_PORT_04_PIN_02, status);
}

bool setHeaterStatus(const uint8_t duty_cycle) {
  if (duty_cycle > 100U) {
    return false;
  }

  fsp_err_t err;

  if (0U == g_timer0_ctrl.open) {
    err = R_GPT_Open(&g_timer0_ctrl, &g_timer0_cfg);
    if (FSP_SUCCESS != err) {
      return false;
    }
  }

  if (0U == duty_cycle) {
    err = R_GPT_Stop(&g_timer0_ctrl);
    if (FSP_SUCCESS != err) {
      return false;
    }

    err = R_GPT_DutyCycleSet(&g_timer0_ctrl, 0U, GPT_IO_PIN_GTIOCA);
    if (FSP_SUCCESS != err) {
      return false;
    }

    err = R_GPT_Reset(&g_timer0_ctrl);
    return FSP_SUCCESS == err;
  }

  timer_info_t info{};
  err = R_GPT_InfoGet(&g_timer0_ctrl, &info);
  if ((FSP_SUCCESS != err) || (0U == info.period_counts)) {
    return false;
  }

  auto duty_cycle_counts = static_cast<uint32_t>(
      static_cast<uint64_t>(info.period_counts) * duty_cycle / 100U);
  if (0U == duty_cycle_counts) {
    duty_cycle_counts = 1U;
  }

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
  return writeOutputPin(BSP_IO_PORT_02_PIN_07, status);
}

bool rotateTurntable(const float deltaDegrees) {
  if (!std::isfinite(deltaDegrees)) {
    return false;
  }

  // 电机驱动只接受"角度大小 + 方向"，这里把带符号角度拆开：
  // 正角度右转（CW），负角度左转（CCW）。之前固定 CW 会把负角度当成
  // 负步数，循环一次都不执行。
  const float magnitude =
      deltaDegrees < 0.0F ? -deltaDegrees : deltaDegrees;
  if (!(magnitude > 0.0F)) {
    return true;
  }

  MotorDev *const motor = MotorDevGet();
  if ((nullptr == motor) || (nullptr == motor->rotate)) {
    return false;
  }

  const auto direction = static_cast<uint8_t>(
      deltaDegrees < 0.0F ? MOTOR_DIRECTION_CCW : MOTOR_DIRECTION_CW);
  motor->rotate(magnitude, direction, 2);
  return true;
}
} // namespace HardwareDataManager

void i2c_callback(i2c_master_callback_args_t *p_args) {
  handleI2cCallback(p_args, g_i2c_master0_ctrl.read, kI2c0Bus);
}

void I2C0WaitTxCplt() { waitI2cCompletion(kI2c0Bus, false); }

void I2C0WaitRxCplt() { waitI2cCompletion(kI2c0Bus, true); }

void sci_i2c_master_callback(i2c_master_callback_args_t *p_args) {
  handleI2cCallback(p_args, g_i2c0_ctrl.read, kI2c1Bus);
}

void I2C1WaitTxCplt() { waitI2cCompletion(kI2c1Bus, false); }

void I2C1WaitRxCplt() { waitI2cCompletion(kI2c1Bus, true); }

void adc0_callback(adc_callback_args_t *p_args) {
  if ((nullptr == p_args) || (ADC_EVENT_SCAN_COMPLETE != p_args->event)) {
    return;
  }
  g_adc0_scan_complete = true;
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
  g_adc0_wait_ok = false;
  if (pdTRUE == xSemaphoreTake(g_adc0_scan_sem, kWaitTimeoutTicks)) {
    g_adc0_wait_ok = g_adc0_scan_complete;
  } else {
    if (g_adc0_ready) {
      (void)R_ADC_ScanStop(&g_adc0_ctrl);
    }
    drainSemaphore(g_adc0_scan_sem);
  }
}
