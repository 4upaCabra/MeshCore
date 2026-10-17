#include <Arduino.h>
#include <Wire.h>
#include <RadioLib.h>

#include "PromicroBoard.h"

#ifdef NRF52_POWER_MANAGEMENT
const PowerMgtConfig power_config = {
  .lpcomp_ain_channel = PWRMGT_LPCOMP_AIN,
  .lpcomp_refsel      = PWRMGT_LPCOMP_REFSEL,
  .voltage_bootlock   = PWRMGT_VOLTAGE_BOOTLOCK,
  .lpcomp_low_refsel  = PWRMGT_LPCOMP_LOW_REFSEL,
};

void PromicroBoard::onShutdownMilliVoltsChanged(uint16_t mv) {
  // LPCOMP is the runtime trigger; its reference stays board-specific.
  (void)mv;
  if (power_config.lpcomp_low_refsel) {
    configureLowVoltageAlert(power_config.lpcomp_ain_channel, power_config.lpcomp_low_refsel);
  }
}

void PromicroBoard::initiateShutdown(uint8_t reason) {
  // Flash LED briefly to indicate shutdown reason
  #if defined(PIN_LED)
    pinMode(PIN_LED, OUTPUT);
    for (int i = 0; i < 3; i++) {
      digitalWrite(PIN_LED, LED_STATE_ON);
      delay(30);
      digitalWrite(PIN_LED, !LED_STATE_ON);
      delay(30);
    }
  #endif

  // Cut power to SX1262
  digitalWrite(SX126X_POWER_EN, LOW);

  // Isolate SX1262 pins to prevent parasitic powering via internal ESD diodes
  pinMode(P_LORA_NSS, INPUT);
  pinMode(P_LORA_RESET, INPUT);
  pinMode(P_LORA_BUSY, INPUT);
  pinMode(P_LORA_MISO, INPUT);
  pinMode(P_LORA_SCLK, INPUT);
  pinMode(P_LORA_MOSI, INPUT);
  if (SX126X_RXEN != RADIOLIB_NC) pinMode(SX126X_RXEN, INPUT);

  if (reason == SHUTDOWN_REASON_LOW_VOLTAGE ||
      reason == SHUTDOWN_REASON_BOOT_PROTECT) {
    configureVoltageWake(power_config.lpcomp_ain_channel, power_config.lpcomp_refsel);
  }

  enterSystemOff(reason);
}
#endif

void PromicroBoard::begin() {
    NRF52BoardDCDC::begin();
    btn_prev_state = HIGH;

    #if defined(PIN_LED)
      pinMode(PIN_LED, OUTPUT);
      digitalWrite(PIN_LED, LED_STATE_ON);
      delay(50);
      digitalWrite(PIN_LED, !LED_STATE_ON);
    #endif

    // Start hardware WDT early (8 seconds timeout)
    NRF_WDT->CONFIG = (WDT_CONFIG_SLEEP_Run << WDT_CONFIG_SLEEP_Pos) |
                      (WDT_CONFIG_HALT_Pause << WDT_CONFIG_HALT_Pos);
    NRF_WDT->CRV = 8 * 32768; // 8 seconds
    NRF_WDT->RREN |= WDT_RREN_RR0_Msk;
    NRF_WDT->TASKS_START = 1;
    NRF_WDT->RR[0] = WDT_RR_RR_Reload;

    pinMode(PIN_VBAT_READ, INPUT);

    #ifdef BUTTON_PIN
      pinMode(BUTTON_PIN, INPUT_PULLUP);
    #endif

    #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
      Wire.setPins(PIN_BOARD_SDA, PIN_BOARD_SCL);
    #endif

    Wire.begin();

    pinMode(SX126X_POWER_EN, OUTPUT);
#ifdef NRF52_POWER_MANAGEMENT
    // Initialise power management and sample the boot voltage for
    // `get pwrmgt.bootmv`; the lockout and runtime LPCOMP alert are configured
    // from the `shutdown_mv` pref before radio_init(), not here.
    pwrmgtInit();
    boot_voltage_mv = getBattMilliVolts();
#endif
    digitalWrite(SX126X_POWER_EN, HIGH);
    delay(15);   // give sx1262 some time to power up
}
