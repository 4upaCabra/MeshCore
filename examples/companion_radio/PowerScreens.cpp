#include "PowerScreens.h"

#include <Arduino.h>

#include "UILang.h"

#ifndef POWER_ON_HINT
  #define POWER_ON_HINT _TR("Press RST to turn on", "Нажмите RST")
#endif
// After a low-battery stop: boards with nRF52 power management wake by
// themselves when USB power comes (see NRF52Board::configureVoltageWake).
#ifndef CHARGE_HINT
  #if defined(NRF52_POWER_MANAGEMENT)
    #define CHARGE_HINT _TR("Plug in USB to turn on", "Подключите USB")
  #else
    #define CHARGE_HINT _TR("Charge, then press RST", "Зарядите, затем RST")
  #endif
#endif

namespace power_screens {

// The UIColor values live in the display drivers: a build without a display
// links none of them, and on targets that keep unused functions (ESP32-C3)
// these screens would not link. Without a display there is nothing to draw.
#ifndef DISPLAY_CLASS

void drawBoot(DisplayDriver*) {}
void drawPowerOff(DisplayDriver*, bool) {}

#else

void drawBoot(DisplayDriver* d) {
  if (!d) return;
  d->startFrame();
#ifdef ST7789
  d->setTextSize(2);
#endif
  d->drawTextCentered(d->width() / 2, 28, _TR("Loading...", "Загрузка..."));
  d->endFrame();
}

void drawPowerOff(DisplayDriver* d, bool low_battery) {
  if (!d) return;
  if (!d->isOn()) d->turnOn();
  const char* hint = low_battery ? CHARGE_HINT : POWER_ON_HINT;
  const int big = (d->width() >= 200) ? 2 : 1;
  const int lh  = d->getLineH(1);
  const int bh  = d->getLineH(big);
  const int total = (low_battery ? lh + 2 : 0) + bh + 4 + lh;
  int y = (d->height() - total) / 2;
  if (y < 0) y = 0;
  d->startFrame();
  d->setColor(UIColor::primary_txt);
  d->setTextSize(1);
  if (low_battery) { d->drawTextCentered(d->width() / 2, y, _TR("Low battery", "Низкий заряд")); y += lh + 2; }
  d->setTextSize(big);
  d->drawTextCentered(d->width() / 2, y, _TR("Turned off", "Выключен"));
  y += bh + 4;
  d->setTextSize(1);
  d->drawTextCentered(d->width() / 2, y, hint);
  d->endFrame();
  if (d->isEink()) {
    d->fullRefreshAndHibernate();   // keep the frame on the panel
  } else {
    delay(3000);                    // a lit screen goes dark with the node
  }
}

#endif  // DISPLAY_CLASS

bool batteryFlat(uint16_t mv, bool external_power, uint16_t shutdown_mv) {
  // Below 1 V the reading itself is wrong (no cell, ADC not ready): never a
  // reason to power off. 2.5 V was the old cut-off and hid LTO/Na-ion cells.
  if (shutdown_mv == 0 || external_power || mv < 1000) return false;
  return mv < shutdown_mv;
}

}  // namespace power_screens
