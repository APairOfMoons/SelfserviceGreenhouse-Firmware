#include "new_thread0.h"

#include <new>
#include <stdint.h>

#ifndef GREENHOUSE_USE_FAKE_HARDWARE
#define GREENHOUSE_USE_FAKE_HARDWARE 1
#endif

#include "app/ExceptionMapping.h"
#include "app/GreenhouseControlLoop.h"
#if GREENHOUSE_USE_FAKE_HARDWARE
#include "logic/FakeHardware.h"
#else
#include "app/HardwareAdapter.h"
#include "hardware/HardwareDataManager.h"
#endif
#include "app/MessageSenderSink.h"
#include "communication/app/MessageSender.h"
#include "configuration/LogicalConfiguration.h"
#include "logic/LogicController.h"
#include "logic/TelemetryReporter.h"
#include "rtt/SEGGER_RTT.h"

namespace {

constexpr TickType_t kLoopPeriodTicks = pdMS_TO_TICKS(100);
constexpr uint32_t kRttLogPeriodMs = 5000UL;

class MessageSenderExceptionSink : public greenhouse::IExceptionSink {
public:
  explicit MessageSenderExceptionSink(MessageSender &sender)
      : sender_(sender) {}

  void throwException(GreenhouseException exception) override {
    sender_.throwGreenhouseException(exception);
  }

private:
  MessageSender &sender_;
};

template <typename T> struct StaticStorage {
  alignas(T) unsigned char raw[sizeof(T)];
};

// 所有大对象与运行时状态统一落在静态存储区（.bss），避免占用仅 1024 B
// 的线程栈， 同时规避局部 static 触发未开启 -fno-threadsafe-statics 时的
// __cxa_guard_* 风险与堆分配。
#if GREENHOUSE_USE_FAKE_HARDWARE
StaticStorage<greenhouse::FakeHardware> g_hardware_storage;
#else
StaticStorage<greenhouse::HardwareAdapter> g_hardware_storage;
#endif
StaticStorage<MessageSender> g_sender_storage;
StaticStorage<greenhouse::MessageSenderSink> g_telemetry_sink_storage;
StaticStorage<MessageSenderExceptionSink> g_exception_sink_storage;
LogicalStatics g_initial_cfg = {};
StaticStorage<greenhouse::LogicController> g_controller_storage;
StaticStorage<greenhouse::TelemetryReporter> g_reporter_storage;
StaticStorage<greenhouse::GreenhouseControlLoop> g_control_loop_storage;

void toFixedDec2(float value, const char **sign, unsigned *whole,
                 unsigned *frac) {
  const float scaled_f = value * 100.0F;
  if (!((scaled_f > -2147483648.0F) && (scaled_f < 2147483648.0F))) {
    *sign = "";
    *whole = 0U;
    *frac = 0U;
    return;
  }

  const bool negative = (scaled_f < 0.0F);
  const float abs_scaled = negative ? -scaled_f : scaled_f;
  const unsigned total = static_cast<unsigned>(abs_scaled + 0.5F);
  *sign = (negative && (total > 0U)) ? "-" : "";
  *whole = total / 100U;
  *frac = total % 100U;
}

void logStatusSummary(const uint32_t now_ms,
                      const greenhouse::ControllerSnapshot &snap) {
  auto t_sign = "";
  unsigned t_whole = 0U;
  unsigned t_frac = 0U;
  toFixedDec2(snap.temperature, &t_sign, &t_whole, &t_frac);

  const char *h_sign = "";
  unsigned h_whole = 0U;
  unsigned h_frac = 0U;
  toFixedDec2(snap.air_humidity, &h_sign, &h_whole, &h_frac);

  const char *s_sign = "";
  unsigned s_whole = 0U;
  unsigned s_frac = 0U;
  toFixedDec2(snap.soil_humidity, &s_sign, &s_whole, &s_frac);

  // new_thread0 栈仅 1024 B：使用 SEGGER_RTT_printf（64 B 栈缓冲，仅整数格式化）
  // 替代 rtt_printf("%f")（256 B 栈缓冲 + libc vsnprintf 浮点格式化），避免栈溢出。
  SEGGER_RTT_printf(
      0U,
      "[ctrl %u ms] T=%s%u.%02uC H=%s%u.%02u S=%s%u.%02u L=%u W=%u F%u L%u "
      "H%u P%u A=%d err=0x%x\r\n",
      static_cast<unsigned>(now_ms), t_sign, t_whole, t_frac, h_sign, h_whole,
      h_frac, s_sign, s_whole, s_frac, static_cast<unsigned>(snap.illuminance),
      snap.has_enough_water ? 1U : 0U, snap.fan_on ? 1U : 0U,
      snap.light_on ? 1U : 0U, snap.heater_on ? 1U : 0U,
      snap.pump_on ? 1U : 0U, static_cast<int>(snap.turn_angle),
      static_cast<unsigned>(snap.faults));
}

void logExceptionEdges(const uint32_t now_ms, const uint32_t rising_mask) {
  if (0U != (rising_mask & greenhouse::EXCEPTION_WATER_LACK)) {
    SEGGER_RTT_printf(0U, "[ctrl %u ms] EXC: WATER_LACK\r\n",
                      static_cast<unsigned>(now_ms));
  }
  if (0U != (rising_mask & greenhouse::EXCEPTION_TURNTABLE_FAILED)) {
    SEGGER_RTT_printf(0U, "[ctrl %u ms] EXC: TURNTABLE_FAILED\r\n",
                      static_cast<unsigned>(now_ms));
  }
}

} // namespace

/* New Thread entry function */
/* pvParameters contains TaskHandle_t */
// ReSharper disable once CppParameterMayBeConstPtrOrRef
void new_thread0_entry(void *pvParameters) {
  FSP_PARAMETER_NOT_USED(pvParameters);

#if GREENHOUSE_USE_FAKE_HARDWARE
  auto *const hardware = new (static_cast<void *>(g_hardware_storage.raw))
      greenhouse::FakeHardware();
#else
  // 必须在调度器运行后的线程上下文初始化外设（内部依赖 vTaskDelay
  // 与静态信号量）， 且 peripheral_init() 会将 5 路执行器置为安全关闭态。
  HardwareDataManager::peripheral_init();

  auto *const hardware = new (static_cast<void *>(g_hardware_storage.raw))
      greenhouse::HardwareAdapter();
#endif
  auto *const sender =
      new (static_cast<void *>(g_sender_storage.raw)) MessageSender();
  auto *const telemetry_sink =
      new (static_cast<void *>(g_telemetry_sink_storage.raw))
          greenhouse::MessageSenderSink(*sender);
  auto *const exception_sink =
      new (static_cast<void *>(g_exception_sink_storage.raw))
          MessageSenderExceptionSink(*sender);

  const uint32_t cfg_version = LogicalConfiguration::version();
  LogicalConfiguration::snapshot(&g_initial_cfg);

  auto *const controller = new (static_cast<void *>(g_controller_storage.raw))
      greenhouse::LogicController(*hardware, g_initial_cfg);

  if (LogicalConfiguration::version() != cfg_version) {
    LogicalConfiguration::snapshot(&g_initial_cfg);
    controller->setConfiguration(g_initial_cfg);
  }

  auto *const reporter = new (static_cast<void *>(g_reporter_storage.raw))
      greenhouse::TelemetryReporter(
          *telemetry_sink,
          greenhouse::GreenhouseControlLoop::defaultReporterConfig());
  auto *const control_loop =
      new (static_cast<void *>(g_control_loop_storage.raw))
          greenhouse::GreenhouseControlLoop(*controller, *reporter,
                                            *exception_sink);

  TickType_t last_wake_tick = xTaskGetTickCount();
  uint32_t last_rtt_log_ms =
      last_wake_tick * (portTICK_PERIOD_MS)-kRttLogPeriodMs;

  while (true) {
    const uint32_t now_ms = xTaskGetTickCount() * (portTICK_PERIOD_MS);

    control_loop->step(now_ms);

    const uint32_t rising_mask = control_loop->lastRisingExceptionMask();
    if (greenhouse::EXCEPTION_NONE != rising_mask) {
      logExceptionEdges(now_ms, rising_mask);
    }

    if (now_ms - last_rtt_log_ms >= kRttLogPeriodMs) {
      last_rtt_log_ms = now_ms;
      logStatusSummary(now_ms, control_loop->snapshot());
    }

    // rotateTurntable()（0.34 s ~ 1.36 s）或传感器超时会导致阻塞超过 100 ms
    // 节拍； 若唤醒目标已落后当前 tick，重同步
    // last_wake_tick，防止单次阻塞后连续补跑多拍。
    const TickType_t after_step_tick = xTaskGetTickCount();
    if (after_step_tick - last_wake_tick >= kLoopPeriodTicks) {
      last_wake_tick = after_step_tick;
    }
    if (pdFALSE == xTaskDelayUntil(&last_wake_tick, kLoopPeriodTicks)) {
      last_wake_tick = xTaskGetTickCount();
    }
  }
}
