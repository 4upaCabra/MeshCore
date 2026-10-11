#include "DisplayPowerHold.h"

// Default: nothing to hold. ESP32 companions that enable power saving provide
// strong definitions that keep the CPU awake around panel work.
#if defined(__GNUC__)
__attribute__((weak)) void displayPowerHoldBegin() {}
__attribute__((weak)) void displayPowerHoldEnd() {}
#else
void displayPowerHoldBegin() {}
void displayPowerHoldEnd() {}
#endif
