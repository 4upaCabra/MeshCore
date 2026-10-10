#pragma once

// Automatic light sleep for always-on ESP32 nodes (repeater, room server).
//
// The same mechanism as the companion: esp_pm_configure() with light sleep and
// DFS 80/40 MHz, plus a yield to the FreeRTOS idle task whenever nothing is due,
// instead of a manual board.sleep(). Needs a core built with CONFIG_PM_ENABLE
// (tools/ps_framework.py); on a stock core esp_pm_configure() reports
// ESP_ERR_NOT_SUPPORTED and the node runs as if power saving were off.
//
// Wake sources: the yield timer, UART RX (boards whose console is a UART through a
// USB bridge) and a poll of the radio IRQ line (RadioLibWrapper::
// recoverMissedDioInterrupt). Not ext1 / gpio_wakeup on the radio line: IDF 4.4
// leaves an ext1 pin in the RTC domain after a light sleep, and a level GPIO wakeup
// would storm the RadioLib ISR.
//
// Keep-awake windows (an ESP_PM_NO_LIGHT_SLEEP lock): after boot, after console
// input, after a press of the user button, and — on boards whose Serial is the
// chip's own USB — while a USB host is attached. That link is serviced by the CPU,
// so it also needs APB at full speed (DFS down to the XTAL turns its PLL off).

#if defined(ESP32) && defined(WITH_ESP32_POWER_SAVING)

#include <Arduino.h>
#if defined(WITH_ESP32_PM_REQUIRED)
  // A *_ps environment: refuse to build a "power saving" image on a core that
  // cannot sleep. Install the PS core once: python tools/ps_framework.py
  #include "sdkconfig.h"
  #if !CONFIG_PM_ENABLE
    #error "WITH_ESP32_PM_REQUIRED: framework built without CONFIG_PM_ENABLE - run: python tools/ps_framework.py"
  #endif
#endif
#include "esp_pm.h"
#include "esp_sleep.h"
#include "driver/uart.h"

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  #define AUTOSLEEP_NATIVE_USB  1
  #if !(defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE)
    #include "USB.h"   // ESPUSB: TinyUSB "mounted" state
  #endif
#else
  #define AUTOSLEEP_NATIVE_USB  0
#endif

// Light sleep only where no link the CPU has to service stays up.
#if defined(WITH_BRIDGE) || defined(ETHERNET_ENABLED) || defined(WIFI_SSID)
  #define AUTOSLEEP_LIGHT_SLEEP 0
#else
  #define AUTOSLEEP_LIGHT_SLEEP 1
#endif

#ifndef AUTOSLEEP_YIELD_MS
  #define AUTOSLEEP_YIELD_MS        50
#endif
#define AUTOSLEEP_CPU_MAX_MHZ       80
#define AUTOSLEEP_CPU_MIN_MHZ       40
#define AUTOSLEEP_SERIAL_HOLD_MS    60000
#define AUTOSLEEP_BTN_HOLD_MS       30000

class ESP32AutoSleep {
  // A keep-awake window: a flag plus a deadline, cleared with an unsigned
  // difference so the ~49.7-day millis() wrap is handled.
  struct Window {
    bool active = false;
    uint32_t until = 0;
    void open(uint32_t ms) { active = true; until = millis() + ms; }
    bool service() {
      if (active && (int32_t)(millis() - until) >= 0) active = false;
      return active;
    }
  };

  const char* _tag;
  esp_pm_lock_handle_t _lock = NULL;      // ESP_PM_NO_LIGHT_SLEEP
  esp_pm_lock_handle_t _apb_lock = NULL;  // ESP_PM_APB_FREQ_MAX (native USB)
  bool _lock_held = false;
  bool _apb_held = false;
  uint8_t _applied = 0xFF;                // 0 = off, 1 = on
  Window _boot, _serial, _btn;

  static bool usbHostPresent() {
#if AUTOSLEEP_NATIVE_USB
  #if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE
    return Serial.isConnected();   // HWCDC: SOF frames from a host
  #else
    return (bool)USB;              // TinyUSB: enumerated and configured by a host
  #endif
#else
    return false;                  // UART console through a USB bridge chip
#endif
  }

  void configure(uint8_t on) {
#if CONFIG_IDF_TARGET_ESP32C3
    esp_pm_config_esp32c3_t cfg;
#elif CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t cfg;
#elif CONFIG_IDF_TARGET_ESP32
    esp_pm_config_esp32_t cfg;
#elif CONFIG_IDF_TARGET_ESP32C6
    esp_pm_config_t cfg;
#else
  #error "No esp_pm_config_t for this target"
#endif
    cfg.max_freq_mhz = AUTOSLEEP_CPU_MAX_MHZ;
    cfg.min_freq_mhz = on ? AUTOSLEEP_CPU_MIN_MHZ : AUTOSLEEP_CPU_MAX_MHZ;  // min == max: no DFS
    cfg.light_sleep_enable = on && (AUTOSLEEP_LIGHT_SLEEP != 0);
    esp_err_t err = esp_pm_configure(&cfg);
    _applied = on;
    if (err == ESP_OK) {
      Serial.printf("%s PM: %d-%d MHz, light sleep %s, %s\n", _tag, (int)cfg.max_freq_mhz,
                    (int)cfg.min_freq_mhz, cfg.light_sleep_enable ? "on" : "off", on ? "on" : "off");
    } else {
      Serial.printf("%s PM: unavailable (%d) - framework without CONFIG_PM_ENABLE\n", _tag, (int)err);
    }
  }

  static void setLock(esp_pm_lock_handle_t lock, bool& held, bool want) {
    if (lock == NULL || want == held) return;
    if (want) esp_pm_lock_acquire(lock); else esp_pm_lock_release(lock);
    held = want;
  }

public:
  explicit ESP32AutoSleep(const char* tag) : _tag(tag) { }

  // Call at the end of setup(). boot_hold_ms keeps the node awake after boot, so
  // the CLI works right after flashing and a native-USB link can enumerate.
  void begin(bool enabled, uint32_t boot_hold_ms) {
    if (esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, _tag, &_lock) != ESP_OK) {
      _lock = NULL;
      Serial.printf("%s PM: no lock available\n", _tag);
    }
#if AUTOSLEEP_NATIVE_USB
    if (esp_pm_lock_create(ESP_PM_APB_FREQ_MAX, 0, _tag, &_apb_lock) != ESP_OK) {
      _apb_lock = NULL;
      Serial.printf("%s PM: no APB lock available\n", _tag);
    }
#else
    // UART0 RX wakes the CPU so the CLI can be typed while asleep. The characters
    // that woke the chip are lost; the window after the last byte covers that.
    esp_err_t err = uart_set_wakeup_threshold(UART_NUM_0, 3);
    if (err != ESP_OK) Serial.printf("%s PM: uart wakeup threshold error (%d)\n", _tag, (int)err);
    err = esp_sleep_enable_uart_wakeup(UART_NUM_0);
    if (err != ESP_OK) Serial.printf("%s PM: uart wakeup enable error (%d)\n", _tag, (int)err);
#endif
#if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0) && !defined(DISPLAY_CLASS)
    pinMode(PIN_USER_BTN, INPUT_PULLUP);   // with a display, MomentaryButton sets it up
#endif
    _boot.open(boot_hold_ms);
    service(enabled);    // take the locks before sleep is switched on
  }

  // Console bytes arrived: call before the CLI loop consumes them.
  void noteSerial() { _serial.open(AUTOSLEEP_SERIAL_HOLD_MS); }

  // Every loop iteration: windows, locks, and `powersaving on|off` without a reboot.
  void service(bool enabled) {
#if defined(PIN_USER_BTN) && (PIN_USER_BTN >= 0)
    if (digitalRead(PIN_USER_BTN) == LOW) _btn.open(AUTOSLEEP_BTN_HOLD_MS);
#endif
    const bool host = usbHostPresent();
    const bool windows = _boot.service() | _serial.service() | _btn.service();
    setLock(_lock, _lock_held, windows || host);
    setLock(_apb_lock, _apb_held, AUTOSLEEP_NATIVE_USB && (windows || host));
    const uint8_t want = enabled ? 1 : 0;
    if (want != _applied) configure(want);
  }

  // At the end of loop(): hand the CPU to the idle task when nothing is due.
  void yieldIfIdle(bool enabled, bool due_work) {
    if (enabled && !due_work) vTaskDelay(pdMS_TO_TICKS(AUTOSLEEP_YIELD_MS));
  }
};

#endif
