#pragma once
#include <Arduino.h>
#include "MyMesh.h"

inline bool uiIsLangRu() {
#ifdef CYRILLIC_SUPPORT
  auto* p = the_mesh.getNodePrefs();
  return p && p->ui_lang == 1;
#else
  return false;
#endif
}

#define _TR(en, ru) (uiIsLangRu() ? (ru) : (en))
