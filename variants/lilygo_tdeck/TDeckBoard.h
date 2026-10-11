#pragma once

#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"

#define PIN_VBAT_READ 4
#define BATTERY_SAMPLES 8
#define BATT_DIVIDER 2.0f   // battery divider ratio, the default multiplier

class TDeckBoard : public ESP32Board {
public:
  void begin();

  #ifdef P_LORA_TX_LED
    void onBeforeTransmit() override{
      digitalWrite(P_LORA_TX_LED, LOW); // turn TX LED on - invert pin for SX1276
    }

    void onAfterTransmit() override{
      digitalWrite(P_LORA_TX_LED, HIGH); // turn TX LED off - invert pin for SX1276
    }
  #endif

  // Battery divider ratio. 0 in adc_mult means the board default; prefs, the CLI
  // (set adc.multiplier) and Cal ADC set it. ESP32Board's versions cannot do
  // this here: they are compiled before PIN_VBAT_READ is defined above.
  bool setAdcMultiplier(float multiplier) override {
    adc_mult = multiplier;
    return true;
  }

  float getAdcMultiplier() const override {
    return (adc_mult == 0.0f) ? BATT_DIVIDER : adc_mult;
  }

  uint16_t getBattMilliVolts() {
    #if defined(PIN_VBAT_READ)
      analogReadResolution(12);

      uint32_t raw = 0;
      for (int i = 0; i < BATTERY_SAMPLES; i++) {
        raw += analogRead(PIN_VBAT_READ);
      }

      raw = raw / BATTERY_SAMPLES;
      return (getAdcMultiplier() * 3.3f * 1000 * raw) / 4096;
    #else
      return 0;
    #endif
  }

  const char* getManufacturerName() const{
    return "LilyGo T-Deck";
  }
};