#pragma once

#include <stddef.h>
#include <stdint.h>

class DisplayDriver;
class NodePrefs;

// Settings folder an extra item is listed in, after the folder's own items.
enum UISettingsFolder : uint8_t {
  UI_FOLDER_DISPLAY    = 0,
  UI_FOLDER_CONNECTION = 1,
  UI_FOLDER_EXTENSION  = 2,   // a folder of the build's own, see uiExtensionFolderLabel()
};

struct UIExtraSetting {
  const char* (*label)();                   // label shown in settings list (may follow the UI language)
  bool (*visible)(DisplayDriver& d);        // nullptr = always visible
  void (*value)(char* buf, size_t len);     // nullptr = action item without value
  void (*press)(bool fwd);                  // press callback: toggle or select, fwd = direction
  uint8_t folder;                           // UISettingsFolder
};

// Returns number of extra settings items. If items is non-null, sets *items to the array.
int  uiExtraSettings(const UIExtraSetting** items);

// Name of the UI_FOLDER_EXTENSION folder in the settings root. nullptr (the
// default) hides the folder; so does a folder with nothing visible in it.
const char* uiExtensionFolderLabel();

// A full-screen view the build may open from one of its settings. While it is
// active the home screen hands it the display and every key.
bool uiOverlayActive();
int  uiOverlayRender(DisplayDriver& d);     // returns the refresh interval in milliseconds
void uiOverlayInput(char key);

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
