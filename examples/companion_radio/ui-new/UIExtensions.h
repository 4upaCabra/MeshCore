#pragma once

#include <stddef.h>

class DisplayDriver;
class NodePrefs;

struct UIExtraSetting {
  const char* label;                        // подпись в списке настроек
  bool (*visible)(DisplayDriver& d);        // nullptr — виден всегда
  void (*value)(char* buf, size_t len);     // nullptr — пункт-действие без значения
  void (*press)();                          // нажатие: переключить / открыть
};

// Returns number of extra settings items. If items is non-null, sets *items to the array.
int  uiExtraSettings(const UIExtraSetting** items);

// Hook at UI initialization (e.g. to apply custom display/prefs settings)
void uiBegin(DisplayDriver& d, NodePrefs* prefs);

// Optional short note displayed on the right of the header. Returns true if note was set.
bool uiHeaderNote(char* buf, size_t len);

// Optional custom page renderer. Returns true if the page was rendered.
bool uiDrawPage(int page, DisplayDriver& d, int y0);

// Total number of selectable timesync source modes. Default: 3 ("a+g", "gps", "adv").
int  uiTimeSourceModeCount();
