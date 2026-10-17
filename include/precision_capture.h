#pragma once
#include <Arduino.h>
#include "clock_math.h"
#ifdef ESP32
#include <driver/mcpwm.h>
#include <soc/mcpwm_struct.h>
#include <esp_pm.h>

bool hardwareCapture = false;
portMUX_TYPE captureTimerMux = portMUX_INITIALIZER_UNLOCKED;
CaptureCounter captureCounter;
inline uint32_t captureNowRaw() {
  // Channel 0 latches PPS; channel 1 is reserved for software reads of the SAME timer.
  MCPWM0.cap_chn_cfg[1].capn_sw = 1;
  return MCPWM0.cap_chn[1].val;
}
inline uint32_t clockMicros() {
  if (!hardwareCapture) return micros();
  portENTER_CRITICAL(&captureTimerMux);
  uint64_t ticks = captureCounter.extend(captureNowRaw());
  portEXIT_CRITICAL(&captureTimerMux);
  return uint32_t(ticks / 80);
}
inline uint32_t capturedEdgeMicros(uint32_t captured) {
  portENTER_CRITICAL(&captureTimerMux);
  uint32_t now = captureNowRaw();
  uint64_t ticks = captureCounter.extend(now);
  portEXIT_CRITICAL(&captureTimerMux);
  return uint32_t((ticks - uint32_t(now - captured)) / 80);
}
inline bool beginPrecisionCapture(int pin, cap_isr_cb_t callback) {
#if CONFIG_PM_ENABLE
  static esp_pm_lock_handle_t apbLock;
  if (esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, "pps-capture", &apbLock) != ESP_OK) return false;
  if (esp_pm_lock_acquire(apbLock) != ESP_OK) { esp_pm_lock_delete(apbLock); return false; }
#endif
  mcpwm_capture_config_t config = {};
  config.cap_edge = MCPWM_POS_EDGE; config.cap_prescale = 1; config.capture_cb = callback;
  if (getApbFrequency() != 80000000 || mcpwm_gpio_init(MCPWM_UNIT_0, MCPWM_CAP_0, pin) != ESP_OK ||
      mcpwm_capture_enable_channel(MCPWM_UNIT_0, MCPWM_SELECT_CAP0, &config) != ESP_OK) {
    mcpwm_capture_disable_channel(MCPWM_UNIT_0, MCPWM_SELECT_CAP0);
#if CONFIG_PM_ENABLE
    esp_pm_lock_release(apbLock); esp_pm_lock_delete(apbLock);
#endif
    return false;
  }
  MCPWM0.cap_chn_cfg[1].capn_en = 1;
  hardwareCapture = true;
  clockMicros(); // Initialize extension before the 32-bit capture timer wraps (~53.7 s).
  return true;
}
#else
constexpr bool hardwareCapture = false;
inline uint32_t clockMicros() { return micros(); }
#endif
