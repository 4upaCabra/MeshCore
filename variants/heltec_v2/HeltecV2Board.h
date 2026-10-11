#pragma once

#include <Arduino.h>
#include <helpers/ESP32Board.h>

// built-ins
#define  PIN_VBAT_READ   37
#define  PIN_LED_BUILTIN 25

class HeltecV2Board : public ESP32Board {
public:
  void begin() {
    ESP32Board::begin();

    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_DEEPSLEEP) {
      long wakeup_source = esp_sleep_get_ext1_wakeup_status();
      if (wakeup_source & (1 << P_LORA_DIO_0)) {  // received a LoRa packet (while in deep sleep)
        startup_reason = BD_STARTUP_RX_PACKET;
      }

      rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
      rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_0);
    }
  }

  // Battery divider ratio. 0 in adc_mult means the board default; prefs, the CLI
  // (set adc.multiplier) and Cal ADC set it. ESP32Board's versions cannot do
  // this here: they are compiled before PIN_VBAT_READ is defined above.
  bool setAdcMultiplier(float multiplier) override {
    adc_mult = multiplier;
    return true;
  }

  float getAdcMultiplier() const override {
    return (adc_mult == 0.0f) ? 1.98f : adc_mult;
  }

  uint16_t getBattMilliVolts() override {
    analogReadResolution(10);

    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) {
      raw += analogRead(PIN_VBAT_READ);
    }
    raw = raw / 8;

    return (getAdcMultiplier() * (2 / 1024.0) * raw) * 1000;
  }

  const char* getManufacturerName() const override {
    return "Heltec V2";
  }

  uint32_t getIRQGpio() override {
    return P_LORA_DIO_0; // default for SX1276
  }
};
