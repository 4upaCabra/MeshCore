#pragma once

#include <GxEPD2_BW.h>
#include "AsyncEinkCommon.h"
#include "Depg0290PartialLut.h"

// Non-blocking extension for GxEPD2 1.6.4's GxEPD2_290_T94_V2 driver.
// This panel has no partial waveform in OTP. Partial updates use the setup of
// the Vision Master E290 (same SSD1680 controller, see initPartialWaveform()):
// GxEPD2's short BN-style table left old black visible, mostly on the dark theme.
class AsyncGxEPD290T94V2 : public GxEPD2_290_T94_V2 {
public:
  using PollResult = AsyncEinkPollResult;

  AsyncGxEPD290T94V2(int16_t cs, int16_t dc, int16_t rst, int16_t busy)
    : GxEPD2_290_T94_V2(cs, dc, rst, busy) {}

  bool startRefreshAsync(bool partial) {
    if (_operation != Operation::None) return false;
    if (partial && !_using_partial_mode) initPartialWaveform();
    if (!partial) {
      // Border back to the GxEPD2 setting (follows the full waveform), so a
      // full refresh still cleans it.
      _writeCommand(0x3C); _writeData(0x05);
      _using_partial_mode = false;
    }
    _writeCommand(0x22);
    _writeData(partial ? 0xcf : 0xf7);   // 0xCF: mode 2 with the LUT in the register
    _writeCommand(0x20);
    start(Operation::Refresh, partial ? partial_refresh_time : full_refresh_time, partial);
    return true;
  }

  bool startPowerOffAsync() {
    if (_operation != Operation::None) return false;
    if (!_power_is_on) {
      _using_partial_mode = false;
      return false;
    }
    _writeCommand(0x22);
    _writeData(0x83);
    _writeCommand(0x20);
    start(Operation::PowerOff, power_off_time, false);
    return true;
  }

  bool asyncBusy() const { return _operation != Operation::None; }

  PollResult pollAsync() {
    if (_operation == Operation::None) return PollResult::Idle;
    const uint32_t elapsed = millis() - _started_at;
    if (elapsed < ASYNC_EINK_BUSY_ASSERT_MS) return PollResult::Busy;
    if (panelBusy()) {
      if (elapsed <= _timeout_ms) return PollResult::Busy;
      completeOperation();
      return PollResult::TimedOut;
    }
    completeOperation();
    return PollResult::Complete;
  }

private:
  enum class Operation : uint8_t { None, Refresh, PowerOff };
  Operation _operation = Operation::None;
  uint32_t _started_at = 0;
  uint32_t _timeout_ms = 0;
  bool _partial_refresh = false;

  void start(Operation operation, uint32_t nominal_ms, bool partial) {
    _operation = operation;
    _started_at = millis();
    _timeout_ms = nominal_ms + ASYNC_EINK_TIMEOUT_SLACK_MS;
    _partial_refresh = partial;
  }

  bool panelBusy() const {
    if (_busy < 0) return (millis() - _started_at) < (_timeout_ms - ASYNC_EINK_TIMEOUT_SLACK_MS);
    return digitalRead(_busy) == _busy_level;
  }

  void completeOperation() {
    if (_operation == Operation::Refresh) {
      _initial_refresh = false;
      _power_is_on = _partial_refresh;
    } else if (_operation == Operation::PowerOff) {
      _power_is_on = false;
      _using_partial_mode = false;
    }
    _operation = Operation::None;
  }

  // The partial-update setup of the Vision Master E290: its waveform drives
  // every changed pixel one way only — no inverted pre-pulse, so no flash — but
  // long enough to bring old black back to white. Loaded again after every
  // panel power-off.
  void initPartialWaveform() {
    _writeCommand(0x3C); _writeData(0x80);   // border held at VCOM: not driven, does not flash
    _writeCommand(0x04); _writeData(0x41); _writeData(0x00); _writeData(0x32);   // source voltages, +-15 V
    _writeCommand(0x32);                                                        // waveform LUT
    for (size_t i = 0; i < sizeof(DEPG0290_LUT_PARTIAL); i++) _writeData(DEPG0290_LUT_PARTIAL[i]);
    _using_partial_mode = true;
  }
};
