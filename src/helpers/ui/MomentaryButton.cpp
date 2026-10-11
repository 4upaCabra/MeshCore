#include "MomentaryButton.h"

#define MULTI_CLICK_WINDOW_MS  280
#define DEBOUNCE_MS            50

// ── Static ISR dispatch ──────────────────────────────────────────────────────
// File-local: up to 8 buttons can register GPIO interrupts.

static MomentaryButton* s_irq_instances[8] = {};
static uint8_t s_irq_count = 0;

static void s_dispatch(uint8_t idx) {
  if (s_irq_instances[idx]) s_irq_instances[idx]->_notifyPressed();
}
static void s_isr0() { s_dispatch(0); }
static void s_isr1() { s_dispatch(1); }
static void s_isr2() { s_dispatch(2); }
static void s_isr3() { s_dispatch(3); }
static void s_isr4() { s_dispatch(4); }
static void s_isr5() { s_dispatch(5); }
static void s_isr6() { s_dispatch(6); }
static void s_isr7() { s_dispatch(7); }

typedef void (*ISRFunc)();
static const ISRFunc s_isr_table[8] = {
  s_isr0, s_isr1, s_isr2, s_isr3, s_isr4, s_isr5, s_isr6, s_isr7
};

// ── Constructor / begin ──────────────────────────────────────────────────────

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, bool reverse, bool pulldownup, bool multiclick) {
  _pin = pin;
  _reverse = reverse;
  _pull = pulldownup;
  down_at = 0;
  prev = _reverse ? HIGH : LOW;
  cancel = 0;
  _long_millis = long_press_millis;
  _threshold = 0;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = multiclick ? MULTI_CLICK_WINDOW_MS : 0;
  _pending_click = false;
}

MomentaryButton::MomentaryButton(int8_t pin, int long_press_millis, int analog_threshold, bool multiclick) {
  _pin = pin;
  _reverse = false;
  _pull = false;
  down_at = 0;
  prev = LOW;
  cancel = 0;
  _long_millis = long_press_millis;
  _threshold = analog_threshold;
  _click_count = 0;
  _last_click_time = 0;
  _multi_click_window = multiclick ? MULTI_CLICK_WINDOW_MS : 0;
  _pending_click = false;
}

void MomentaryButton::begin() {
  if (_pin >= 0 && _threshold == 0) {
    pinMode(_pin, _pull ? (_reverse ? INPUT_PULLUP : INPUT_PULLDOWN) : INPUT);
  }
}

void MomentaryButton::enableInterrupt() {
  if (_pin < 0 || _threshold != 0) return;  // digital pins only
  if (s_irq_count >= 8) return;
  uint8_t idx = s_irq_count++;
  s_irq_instances[idx] = this;
  // For reverse=true (pullup) buttons: press = pin LOW → FALLING edge.
  // For normal (pulldown) buttons: press = pin HIGH → RISING edge.
  // Mode passed as a literal: its type differs between cores (int on nRF52/ESP32,
  // PinStatus on RP2040), so it must not go through an int variable.
  if (_reverse) attachInterrupt(digitalPinToInterrupt(_pin), s_isr_table[idx], FALLING);
  else          attachInterrupt(digitalPinToInterrupt(_pin), s_isr_table[idx], RISING);
}

// ── isPressed ────────────────────────────────────────────────────────────────

bool MomentaryButton::isPressed() const {
  int btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);
  return isPressed(btn);
}

void MomentaryButton::cancelClick() {
  cancel = 1;
  down_at = 0;
  _click_count = 0;
  _last_click_time = 0;
  _pending_click = false;
}

bool MomentaryButton::isPressed(int level) const {
  if (_threshold > 0) {
    return level;
  }
  if (_reverse) {
    return level == LOW;
  } else {
    return level != LOW;
  }
}

// ── check() ──────────────────────────────────────────────────────────────────

int MomentaryButton::check(bool repeat_click) {
  if (_pin < 0) return BUTTON_EVENT_NONE;

  int event = BUTTON_EVENT_NONE;
  int btn = _threshold > 0 ? (analogRead(_pin) < _threshold) : digitalRead(_pin);
  bool press_click = _long_millis == 0 && _multi_click_window == 0;
  bool press_click_armed = true;
#if defined(WITH_ASYNC_EINK)
  if (press_click) press_click_armed = _press_click_armed;
#endif

  // ISR captured a press edge during a blocking call (e.g. eInk refresh).
  // Legacy (long-press / multi-click buttons): if the button is currently UP the
  // full press+release was missed by the poll — synthesize a pending click.
  // press_click buttons are handled after the poll block below.
  if (_irq_pressed && !press_click) {
    if (!isPressed(btn) && down_at == 0 && !cancel) {
      unsigned long now_irq = millis();
      if ((now_irq - _last_click_emitted_ms) >= DEBOUNCE_MS) {
        _click_count++;
        _last_click_time = now_irq;
        _pending_click = true;
      }
    }
    _irq_pressed = false;
  }

  if (btn != prev) {
    unsigned long now = millis();
    _last_change_ms = now;
    if (isPressed(btn)) {
      down_at = now;
      _irq_pressed = false;  // normal DOWN detected; IRQ flag is redundant
      if (press_click) {
        // Fire CLICK on press edge for instant response, but suppress within
        // cooldown to prevent bounce-induced double-fire.
        if (press_click_armed && (now - _last_click_emitted_ms) >= DEBOUNCE_MS) {
          event = BUTTON_EVENT_CLICK;
          _last_click_emitted_ms = now;
#if defined(WITH_ASYNC_EINK)
          _press_click_armed = false;
#endif
        }
      }
    } else {
      // button UP
      unsigned long held_ms = (down_at > 0) ? (now - down_at) : 0;
      if (_long_millis > 0) {
        if (down_at > 0 && held_ms < _long_millis) {
            // Only count as click if button was held long enough to filter
            // out mechanical bounce cycles (< DEBOUNCE_MS).
            if (held_ms >= DEBOUNCE_MS) {
              _click_count++;
              _last_click_time = now;
              _pending_click = true;
            }
        }
      } else if (!press_click) {
          // Legacy: long==0 (with or without multi-click) → CLICK/count on release.
          if (held_ms >= DEBOUNCE_MS) {
            _click_count++;
            _last_click_time = now;
            _pending_click = true;
          }
      }
      // else (press_click): CLICK already fired on the press edge above.
      if (event == BUTTON_EVENT_CLICK && cancel) {
        event = BUTTON_EVENT_NONE;
        _click_count = 0;
        _last_click_time = 0;
        _pending_click = false;
      }
      down_at = 0;
    }
    prev = btn;
  }

  // press_click recovery: the ISR captured a press edge that the poll could not
  // surface — happens when a release+next-press both occur during a blocking
  // render, leaving prev stuck DOWN so the new press is not seen as an edge.
  // Fire the missed CLICK and resync. Verify pin is actually pressed to prevent
  // phantom clicks from noise-triggered interrupts.
  if (_irq_pressed && press_click) {
    _irq_pressed = false;
    unsigned long now_irq = millis();
    if (event == BUTTON_EVENT_NONE && press_click_armed && isPressed(btn)
        && (now_irq - _last_click_emitted_ms) >= DEBOUNCE_MS) {
      event = BUTTON_EVENT_CLICK;
      _last_click_emitted_ms = now_irq;
#if defined(WITH_ASYNC_EINK)
      _press_click_armed = false;
#endif
      prev = btn;
      down_at = now_irq;
    }
  }

#if defined(WITH_ASYNC_EINK)
  // Re-arm only after a continuously stable release. Mechanical bounce may
  // cross the electrical threshold several times; every raw transition above
  // updates _last_change_ms, extending this quiet period.
  if (press_click && !_press_click_armed && !isPressed(btn)
      && (unsigned long)(millis() - _last_change_ms) >= DEBOUNCE_MS) {
    _press_click_armed = true;
  }
#endif

  if (!isPressed(btn) && cancel) {
    cancel = 0;
  }

  if (_long_millis > 0 && down_at > 0 && (unsigned long)(millis() - down_at) >= _long_millis) {
    if (_pending_click) {
      cancelClick();
    } else if ((millis() - _last_click_emitted_ms) >= DEBOUNCE_MS) {
      event = BUTTON_EVENT_LONG_PRESS;
      _last_click_emitted_ms = millis();
      down_at = 0;
      _click_count = 0;
      _last_click_time = 0;
      _pending_click = false;
      cancel = 1;  // suppress IRQ-synthesised click from button bounce during eink refresh
    }
  }
  if (down_at > 0 && repeat_click) {
    unsigned long diff = (unsigned long)(millis() - down_at);
    if (diff >= 700) {
      event = BUTTON_EVENT_CLICK;
    }
  }

  if (_pending_click && (millis() - _last_click_time) >= (unsigned long)_multi_click_window) {
    if (down_at > 0) {
      return event;
    }
    unsigned long now_emit = millis();
    if ((now_emit - _last_click_emitted_ms) < DEBOUNCE_MS) {
      return event;  // suppress within cooldown
    }
    switch (_click_count) {
      case 1:  event = BUTTON_EVENT_CLICK;        break;
      case 2:  event = BUTTON_EVENT_DOUBLE_CLICK; break;
      case 3:  event = BUTTON_EVENT_TRIPLE_CLICK; break;
      default: event = BUTTON_EVENT_TRIPLE_CLICK; break;
    }
    if (event != BUTTON_EVENT_NONE) _last_click_emitted_ms = now_emit;
    _click_count = 0;
    _last_click_time = 0;
    _pending_click = false;
  }

  return event;
}
