#pragma once

#include <stddef.h>

class DisplayDriver;
class NodePrefs;

struct UIExtraSetting {
  const char* label;                        // label shown in settings list
  bool (*visible)(DisplayDriver& d);        // nullptr = always visible
  void (*value)(char* buf, size_t len);     // nullptr = action item without value
  void (*press)(bool fwd);                  // press callback: toggle or select, fwd = direction
};

// Returns number of extra settings items. If items is non-null, sets *items to the array.
int  uiExtraSettings(const UIExtraSetting** items);

// Hook at UI initialization (e.g. to apply custom display or prefs settings).
void uiBegin(DisplayDriver& d, NodePrefs* prefs);

// Optional short note displayed on the right of the header for the given page.
// Returns true if note was populated.
bool uiHeaderNote(int page, char* buf, size_t len);

// Optional custom page renderer.
// Returns 0 if not rendered, or refresh interval in milliseconds.
int  uiDrawPage(int page, DisplayDriver& d, int y0);

// Total number of selectable timesync source modes. Default: 3 ("a+g", "gps", "adv").
int  uiTimeSourceModeCount();

// Label for extra timesync source modes beyond the standard 3. Default: nullptr.
const char* uiTimeSourceModeLabel(int m);

// Optional custom renderer for message body content (e.g. rich media, images).
// Returns true if content was rendered, false to fall back to plain text word wrap.
bool uiDrawMessageContent(DisplayDriver& d, const char* msg, int x, int y, int w, int h);
