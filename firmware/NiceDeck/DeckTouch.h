#pragma once
#include <stdint.h>

// Touch sample conditioning for the CST816D, kept free of Arduino and LVGL so
// recorded and synthetic sample streams can be replayed on the host without
// sending USB shortcuts.
//
// This layer deliberately does NOT decide what a tap is. It answers one
// question -- "is a finger down, and where" -- and hands that to LVGL, which
// owns press/release, drag-off cancellation and click delivery. Firmware up to
// 1.0.4 instead recognised taps here, and rejected a contact whose coordinate
// ever moved more than 24px from where it started. On a 240x320 panel measuring
// about 30.6 x 40.8 mm that threshold is roughly 3mm of centroid travel, which
// a fingertip exceeds routinely as its contact patch grows and peels away. On
// the connected device that gate rejected 106 of 106 real contacts. Movement
// within the pressed target is normal and is no longer penalised.
namespace decktouch {

constexpr int ScreenWidth = 240, ScreenHeight = 320;

// The single authoritative panel-to-screen transform.
//
// The board is mounted with the USB connector facing down, which is the
// controller's native frame rotated by 180 degrees. Adafruit_ST7789 is given
// the matching setRotation(2) and nothing else in the firmware rotates,
// mirrors or offsets touch coordinates, so display and touch can only ever
// rotate together.
inline void rawToScreen(int rawX, int rawY, int &x, int &y) {
  x = (ScreenWidth - 1) - rawX;
  y = (ScreenHeight - 1) - rawY;
}

inline bool rawInRange(int rawX, int rawY) {
  return rawX >= 0 && rawX < ScreenWidth && rawY >= 0 && rawY < ScreenHeight;
}

inline int magnitude(int n) { return n < 0 ? -n : n; }

// Why a contact was refused, for the diagnostic report. Nothing here changes
// behaviour; it exists so the next person can see which rule fired instead of
// guessing from counters.
enum class Reject : uint8_t { None, Gated, ShortPulse, Stale, Outlier };

struct Contact {
  bool pressed;   // what LVGL is told
  int x, y;       // screen coordinates of the reported point
  bool changed;   // pressed state differs from the previous update
  Contact() : pressed(false), x(0), y(0), changed(false) {}
  Contact(bool isPressed, int px, int py, bool didChange)
      : pressed(isPressed), x(px), y(py), changed(didChange) {}
};

class ContactFilter {
public:
  struct Tuning {
    // A real fingertip is in contact for well over 60ms. Requiring the contact
    // to survive two consecutive reads rejects an isolated spurious sample
    // without delaying a genuine press perceptibly. This is a presence check,
    // not a debounce on the action.
    uint32_t confirmMs = 18;
    // The CST816D reports at about 100Hz and momentarily reads back zero
    // fingers between reports. Bridging a short gap keeps one physical touch
    // from being split into several contacts.
    uint32_t releaseMs = 40;
    // Beyond this the interaction is a hold, not a tap, and the click is
    // suppressed even though LVGL still sees a clean release.
    uint32_t holdMs = 1200;
    // No successful read for this long means the finger state is unknown.
    uint32_t staleMs = 150;
    // A single sample this far from the previous one is not believed until a
    // second sample agrees with it. This rejects a lone glitch; it does not
    // smooth or lag genuine movement.
    int outlierPx = 70;
    Tuning() {}
  };

  explicit ContactFilter(Tuning tuning = Tuning()) : t(tuning) {}

  const Tuning &tuning() const { return t; }
  bool pressed() const { return reported; }
  int x() const { return pointX; }
  int y() const { return pointY; }
  // True once the current contact has been held past holdMs. Latches until the
  // contact ends so the click is suppressed however late the release arrives.
  bool holdExceeded() const { return heldTooLong; }
  Reject lastReject() const { return reject; }
  // Why the most recent contact ended. Unlike lastReject() this is not set by
  // transient conditions during a contact, so it can be trusted at release.
  Reject endReason() const { return ended; }
  // True when the current or just-ended contact must not produce a click: it
  // was a hold, or it was cut short by a gate or by lost reads. LVGL still
  // sees a clean release so its own pressed state clears normally; only the
  // action is withheld.
  bool clickInhibited() const {
    return heldTooLong || ended == Reject::Gated || ended == Reject::Stale;
  }
  uint32_t contacts() const { return contactCount; }
  uint32_t rejectedContacts() const { return rejectedCount; }

  // Refuse new contacts until the finger has been seen clearly off the glass.
  // Used for wake, reconnect, configuration apply, page change and diagnostics
  // so none of them can deliver a click. This is the only suppression path.
  void gate(uint32_t now) {
    blocked = true;
    lastGood = now;
    endContact(now, Reject::Gated);
  }

  void reset(uint32_t now) {
    blocked = false;
    phase = Phase::Idle;
    reported = false;
    heldTooLong = false;
    reject = Reject::None;
    ended = Reject::None;
    lastGood = now;
    suspect = false;
  }

  // One conditioned sample. `valid` is a successful I2C transaction, `down` is
  // its finger count, and x/y are already in screen coordinates.
  Contact update(bool valid, bool down, int x, int y, uint32_t now) {
    bool was = reported;

    if (!valid) {
      // A single failed transaction is not a release. A sustained outage means
      // the finger state is unknown, so end the contact and demand a clean
      // release before anything new is accepted.
      if (now - lastGood >= t.staleMs && phase != Phase::Idle) {
        endContact(now, Reject::Stale);
        blocked = true;
      }
      return {reported, pointX, pointY, reported != was};
    }
    lastGood = now;

    if (blocked) {
      if (!down) { blocked = false; phase = Phase::Idle; reject = Reject::None; }
      return {reported, pointX, pointY, reported != was};
    }

    if (down) {
      if (phase == Phase::Idle) {
        phase = Phase::Pending;
        startTime = now;
        pointX = x; pointY = y;
        suspect = false;
        heldTooLong = false;
        reject = Reject::None;
        ended = Reject::None;
        return {reported, pointX, pointY, false};
      }

      // Releasing was provisional; the finger came back within releaseMs.
      liftTime = 0;

      if (magnitude(x - pointX) > t.outlierPx || magnitude(y - pointY) > t.outlierPx) {
        if (!suspect) {
          // Hold the previous point and wait for the next sample to agree.
          suspect = true;
          pendingX = x; pendingY = y;
          reject = Reject::Outlier;
          return {reported, pointX, pointY, false};
        }
        if (magnitude(x - pendingX) <= t.outlierPx && magnitude(y - pendingY) <= t.outlierPx) {
          pointX = x; pointY = y;  // Confirmed: the finger really did move.
        }
        suspect = false;
      } else {
        suspect = false;
        pointX = x; pointY = y;
      }

      if (phase == Phase::Pending && now - startTime >= t.confirmMs) {
        phase = Phase::Down;
        reported = true;
        contactCount++;
      }
      if (phase == Phase::Down && now - startTime >= t.holdMs) heldTooLong = true;
      return {reported, pointX, pointY, reported != was};
    }

    // Finger not detected.
    if (phase == Phase::Idle) return {reported, pointX, pointY, false};
    if (!liftTime) { liftTime = now; return {reported, pointX, pointY, false}; }
    if (now - liftTime < t.releaseMs) return {reported, pointX, pointY, false};
    endContact(now, phase == Phase::Pending ? Reject::ShortPulse : Reject::None);
    return {reported, pointX, pointY, reported != was};
  }

private:
  enum class Phase : uint8_t { Idle, Pending, Down };

  void endContact(uint32_t, Reject why) {
    if (phase == Phase::Pending && why != Reject::None) rejectedCount++;
    if (phase != Phase::Idle) ended = why;
    phase = Phase::Idle;
    reported = false;
    liftTime = 0;
    suspect = false;
    reject = why;
  }

  Tuning t;
  Phase phase = Phase::Idle;
  bool reported = false, blocked = false, suspect = false, heldTooLong = false;
  int pointX = 0, pointY = 0, pendingX = 0, pendingY = 0;
  uint32_t startTime = 0, liftTime = 0, lastGood = 0;
  uint32_t contactCount = 0, rejectedCount = 0;
  Reject reject = Reject::None, ended = Reject::None;
};

}  // namespace decktouch
