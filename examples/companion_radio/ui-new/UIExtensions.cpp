#include "UIExtensions.h"

__attribute__((weak)) int uiExtraSettings(const UIExtraSetting** items) {
  if (items) *items = nullptr;
  return 0;
}

__attribute__((weak)) void uiBegin(DisplayDriver& d, NodePrefs* prefs) {
  (void)d;
  (void)prefs;
}

__attribute__((weak)) bool uiHeaderNote(int page, char* buf, size_t len) {
  (void)page;
  (void)buf;
  (void)len;
  return false;
}

__attribute__((weak)) int uiDrawPage(int page, DisplayDriver& d, int y0) {
  (void)page;
  (void)d;
  (void)y0;
  return 0;
}

__attribute__((weak)) int uiTimeSourceModeCount() {
  return 3;
}

__attribute__((weak)) const char* uiTimeSourceModeLabel(int m) {
  (void)m;
  return nullptr;
}
