#pragma once

#include <helpers/ui/DisplayDriver.h>

// Start and stop screens of the companion.
namespace power_screens {

// Boot: shown once the display is up, before the radio starts.
void drawBoot(DisplayDriver* d);

// Power-off: a last frame saying the node is off, why (low battery) and how to
// turn it on. On e-ink it is pushed with a full refresh and the panel left
// powered down, so it stays readable; the caller then powers the node off.
void drawPowerOff(DisplayDriver* d, bool low_battery);

}  // namespace power_screens
