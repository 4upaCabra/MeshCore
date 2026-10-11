#pragma once

// Panel work that must not be interrupted.
//
// An e-ink refresh is not a memory write: the controller drives a waveform for
// seconds, and the libraries wait it out with delays. On an ESP32 with automatic
// light sleep those delays are exactly the window the idle task uses to put the
// CPU to sleep — mid-refresh. The panel is then left stuck on the first phase of
// the waveform (a black screen that never resolves), because the transfer that
// was supposed to finish it never happened.
//
// Drivers therefore bracket their panel operations with these calls. On ESP32
// companions with power saving enabled the application implements them to hold
// its no-light-sleep lock; everywhere else the weak defaults below do nothing,
// so drivers stay free of platform details.
void displayPowerHoldBegin();
void displayPowerHoldEnd();
