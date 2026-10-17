#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <stdint.h>

// Start and stop screens of the companion.
namespace power_screens {

// Boot: shown once the display is up, before the radio starts.
void drawBoot(DisplayDriver* d);

// Power-off: a last frame saying the node is off, why (low battery) and how to
// turn it on. On e-ink it is pushed with a full refresh and the panel left
// powered down, so it stays readable; the caller then powers the node off.
void drawPowerOff(DisplayDriver* d, bool low_battery);

// Whether a battery at `mv` is below the user's `shutdown_mv` preference.
// 0 disables the check. Never on external power, nor below ~2.5 V, which means
// no battery (none fitted, no sense line).
bool batteryFlat(uint16_t mv, bool external_power, uint16_t shutdown_mv);

}  // namespace power_screens
