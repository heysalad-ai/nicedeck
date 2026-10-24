// Host tests for the pure touch layers: the single coordinate transform, the
// shared screen geometry, and the contact filter that conditions CST816D
// samples before LVGL sees them. Nothing here touches Arduino, LVGL or USB, so
// recorded and synthetic sample streams can be replayed without any chance of
// sending a shortcut.
#include "../firmware/NiceDeck/DeckIcons.h"
#include "../firmware/NiceDeck/DeckLayout.h"
#include "../firmware/NiceDeck/DeckTouch.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {
int checks = 0;
int failures = 0;

void expect(bool condition, const std::string &message) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
  }
}

using decktouch::ContactFilter;
using decktouch::Reject;

// Poll cadence the firmware achieves on hardware: about 8 ms between reads
// while a contact is present, measured as 46,384 polls across 106 contacts.
constexpr std::uint32_t Poll = 8;

// Drive a filter with `count` identical samples, returning the final contact.
decktouch::Contact feed(ContactFilter &filter, bool valid, bool down, int x, int y,
                        std::uint32_t &clock, int count, std::uint32_t step = Poll) {
  decktouch::Contact contact;
  for (int i = 0; i < count; ++i) {
    contact = filter.update(valid, down, x, y, clock);
    clock += step;
  }
  return contact;
}

// --- Coordinate transform --------------------------------------------------
void transform() {
  int x = 0, y = 0;
  decktouch::rawToScreen(0, 0, x, y);
  expect(x == 239 && y == 319, "raw origin maps to the opposite corner for USB-down mounting");
  decktouch::rawToScreen(239, 319, x, y);
  expect(x == 0 && y == 0, "raw far corner maps to the screen origin");
  decktouch::rawToScreen(120, 160, x, y);
  expect(x == 119 && y == 159, "panel centre stays at the centre");

  // Applying the transform twice must return the original point, which is what
  // makes it a rotation and not an ad hoc offset.
  int rx = 0, ry = 0;
  decktouch::rawToScreen(37, 211, x, y);
  decktouch::rawToScreen(x, y, rx, ry);
  expect(rx == 37 && ry == 211, "the transform is its own inverse");

  expect(decktouch::rawInRange(0, 0) && decktouch::rawInRange(239, 319),
         "in-range raw coordinates are accepted");
  expect(!decktouch::rawInRange(240, 0) && !decktouch::rawInRange(0, 320) &&
             !decktouch::rawInRange(-1, 5),
         "out-of-range raw coordinates are rejected");
}

// --- Screen geometry -------------------------------------------------------
void geometry() {
  using namespace decklayout;
  for (int i = 0; i < Tiles; ++i) {
    int cx = tileX(i) + TileWidth / 2, cy = tileY(i) + TileHeight / 2;
    expect(tileAt(cx, cy) == i, "tile " + std::to_string(i) + " centre hits itself");
    expect(tileAt(tileX(i), tileY(i)) == i, "tile " + std::to_string(i) + " top-left corner hits itself");
    expect(tileAt(tileX(i) + TileWidth - 1, tileY(i) + TileHeight - 1) == i,
           "tile " + std::to_string(i) + " bottom-right corner hits itself");
  }

  // Every pixel may belong to at most one tile, and the gaps between tiles are
  // covered rather than dead.
  std::vector<int> owner((std::size_t)Width * Height, -1);
  int covered = 0;
  for (int y = 0; y < Height; ++y) {
    for (int x = 0; x < Width; ++x) {
      int tile = tileAt(x, y);
      owner[(std::size_t)y * (std::size_t)Width + (std::size_t)x] = tile;
      if (tile >= 0) ++covered;
    }
  }
  expect(covered == Tiles * (TileWidth + 2 * TileTouchExtend) * (TileHeight + 2 * TileTouchExtend),
         "extended tile areas tile the grid without overlapping");

  int gapX = tileX(0) + TileWidth + Gap / 2 - 1;  // inside the column gap
  expect(tileAt(gapX, tileY(0) + TileHeight / 2) == 0, "left half of the column gap belongs to the left tile");
  expect(tileAt(gapX + 1, tileY(0) + TileHeight / 2) == 1, "right half of the column gap belongs to the right tile");

  expect(tileAt(Width / 2, 4) == -1, "the header is not a tile target");
  expect(tileAt(Width / 2, NavTop + NavHeight / 2) == -1, "the navigation bar is not a tile target");

  expect(navAt(Margin + 2, NavTop + 2) == 0, "previous-page control responds");
  expect(navAt(Width - Margin - 2, NavTop + NavHeight - 2) == 1, "next-page control responds");
  expect(navAt(Width / 2, NavTop + NavHeight / 2) == -1, "the page indicator between them is not a control");
  expect(navAt(Margin + 2, GridTop) == -1, "the navigation controls do not extend into the grid");

  // The alignment test must not depend on the shortcut hit map.
  for (int i = 0; i < AlignmentTargets; ++i) {
    int tx = AlignmentTargetXY[i][0], ty = AlignmentTargetXY[i][1];
    expect(alignmentHit(i, tx, ty), "alignment target " + std::to_string(i) + " accepts its own centre");
    expect(alignmentHit(i, tx + AlignmentRadius - 1, ty), "alignment target accepts a point inside its radius");
    expect(!alignmentHit(i, tx + AlignmentRadius + 2, ty), "alignment target rejects a point outside its radius");
    for (int j = 0; j < AlignmentTargets; ++j)
      if (i != j) expect(!alignmentHit(j, tx, ty), "alignment targets do not overlap each other");
  }
  // Target 2 sits on the column boundary on purpose: the previous firmware
  // could not accept it because it advanced only on a tile hit.
  expect(alignmentHit(2, AlignmentTargetXY[2][0], AlignmentTargetXY[2][1]),
         "a target on the tile boundary is still reachable");
}

// --- Contact filter --------------------------------------------------------
void deliberateTap() {
  for (int i = 0; i < decklayout::Tiles; ++i) {
    ContactFilter filter;
    std::uint32_t clock = 1000;
    filter.reset(clock);
    int cx = decklayout::tileX(i) + decklayout::TileWidth / 2;
    int cy = decklayout::tileY(i) + decklayout::TileHeight / 2;

    decktouch::Contact contact = filter.update(true, true, cx, cy, clock);
    expect(!contact.pressed, "a press is not reported from a single first sample");
    clock += Poll;
    contact = feed(filter, true, true, cx, cy, clock, 3);
    expect(contact.pressed, "tile " + std::to_string(i) + " reports pressed once the contact is confirmed");
    expect(filter.x() == cx && filter.y() == cy, "the reported point is the touched point");

    // A finger normally rests for over 100 ms. The centroid drifts while it
    // does; firmware 1.0.4 cancelled the tap for exactly this movement.
    contact = feed(filter, true, true, cx + 18, cy + 26, clock, 8);
    expect(contact.pressed, "centroid drift inside the tile keeps the press alive");
    expect(!filter.holdExceeded(), "a normal tap is not treated as a hold");

    contact = feed(filter, true, false, 0, 0, clock, 8);
    expect(!contact.pressed, "release is reported after the finger leaves");
    expect(filter.lastReject() == Reject::None, "a deliberate tap is not recorded as rejected");
    expect(filter.contacts() == 1, "one physical touch counts as one contact");
  }
}

void repeatedTaps() {
  ContactFilter filter;
  std::uint32_t clock = 500;
  filter.reset(clock);
  for (int i = 0; i < 12; ++i) {
    feed(filter, true, true, 62, 80, clock, 12);
    expect(filter.pressed(), "repeated tap " + std::to_string(i) + " presses");
    feed(filter, true, false, 0, 0, clock, 8);
    expect(!filter.pressed(), "repeated tap " + std::to_string(i) + " releases");
  }
  expect(filter.contacts() == 12, "twelve taps produce twelve contacts");
  expect(filter.rejectedContacts() == 0, "no genuine tap in the run was rejected");
}

void noisePulses() {
  // An isolated spurious sample, of the kind movement produces, must not
  // become a press.
  ContactFilter filter;
  std::uint32_t clock = 2000;
  filter.reset(clock);
  for (int i = 0; i < 20; ++i) {
    filter.update(true, true, 100, 200, clock);
    clock += Poll;
    decktouch::Contact contact = feed(filter, true, false, 0, 0, clock, 8);
    expect(!contact.pressed, "an isolated one-sample contact never presses");
  }
  expect(filter.contacts() == 0, "noise pulses are not counted as contacts");
  expect(filter.rejectedContacts() == 20, "each noise pulse is recorded as a rejected contact");
  expect(filter.lastReject() == Reject::ShortPulse, "the rejection reason identifies the short pulse");
}

void dropoutBridging() {
  // The controller reads back zero fingers between its ~100 Hz reports. One
  // physical touch must stay one contact.
  ContactFilter filter;
  std::uint32_t clock = 3000;
  filter.reset(clock);
  feed(filter, true, true, 62, 80, clock, 4);
  expect(filter.pressed(), "contact established");
  filter.update(true, false, 0, 0, clock);
  clock += Poll;
  expect(filter.pressed(), "a single zero-finger read does not end the contact");
  feed(filter, true, true, 63, 81, clock, 3);
  expect(filter.pressed(), "the contact continues when the finger reappears");
  feed(filter, true, false, 0, 0, clock, 10);
  expect(!filter.pressed(), "a sustained absence does end the contact");
  expect(filter.contacts() == 1, "a bridged dropout is still one contact");
}

void missingReads() {
  ContactFilter filter;
  std::uint32_t clock = 4000;
  filter.reset(clock);
  feed(filter, true, true, 62, 80, clock, 4);
  expect(filter.pressed(), "contact established before the bus fails");

  feed(filter, false, false, 0, 0, clock, 2);
  expect(filter.pressed(), "one failed transaction is not treated as a release");

  feed(filter, false, false, 0, 0, clock, 30);
  expect(!filter.pressed(), "a sustained read failure ends the contact");
  expect(filter.lastReject() == Reject::Stale, "the rejection reason identifies lost reads");

  // After an outage the finger state is unknown, so a fresh release is
  // required before anything can press again.
  feed(filter, true, true, 62, 80, clock, 10);
  expect(!filter.pressed(), "a contact already in progress is not adopted after an outage");
  feed(filter, true, false, 0, 0, clock, 3);
  feed(filter, true, true, 62, 80, clock, 5);
  expect(filter.pressed(), "a clean release re-arms the filter");
}

void longHold() {
  ContactFilter filter;
  std::uint32_t clock = 5000;
  filter.reset(clock);
  feed(filter, true, true, 62, 80, clock, 4);
  expect(filter.pressed() && !filter.holdExceeded(), "a short press is not yet a hold");
  feed(filter, true, true, 62, 80, clock, 200);
  expect(filter.pressed(), "the contact stays pressed while held");
  expect(filter.holdExceeded(), "a long hold is flagged so its click can be suppressed");
  feed(filter, true, false, 0, 0, clock, 8);
  expect(!filter.pressed(), "the hold releases normally");
}

void outlierRejection() {
  ContactFilter filter;
  std::uint32_t clock = 6000;
  filter.reset(clock);
  feed(filter, true, true, 62, 80, clock, 4);
  expect(filter.pressed(), "contact established");

  // A single wild reading must not drag the press across the screen.
  filter.update(true, true, 200, 300, clock);
  clock += Poll;
  expect(filter.x() == 62 && filter.y() == 80, "a lone jump is not believed");
  expect(filter.lastReject() == Reject::Outlier, "the jump is reported as an outlier");

  filter.update(true, true, 63, 81, clock);
  clock += Poll;
  expect(filter.x() == 63 && filter.y() == 81, "the contact resumes from its real position");

  // A genuine, sustained move is accepted on the second agreeing sample.
  filter.update(true, true, 190, 250, clock);
  clock += Poll;
  filter.update(true, true, 192, 252, clock);
  clock += Poll;
  expect(filter.x() == 192 && filter.y() == 252, "a confirmed movement is accepted");
}

void gating() {
  ContactFilter filter;
  std::uint32_t clock = 7000;
  filter.reset(clock);

  // Wake, reconnect, page change and configuration apply all gate the filter.
  // A finger already on the glass must not press when the gate lifts.
  feed(filter, true, true, 62, 80, clock, 4);
  expect(filter.pressed(), "pressed before gating");
  filter.gate(clock);
  expect(!filter.pressed(), "gating drops the current press immediately");
  expect(filter.lastReject() == Reject::Gated, "the rejection reason identifies the gate");

  feed(filter, true, true, 62, 80, clock, 20);
  expect(!filter.pressed(), "a finger held through the gate cannot press");

  feed(filter, true, false, 0, 0, clock, 2);
  feed(filter, true, true, 62, 80, clock, 5);
  expect(filter.pressed(), "a new touch after a clean release is accepted");
}

// --- Icons -----------------------------------------------------------------
void icons() {
  const char *names[] = {"key", "copy", "paste", "mic", "video", "play", "volume", "music",
                         "globe", "app", "terminal", "bolt", "folder", "star", "heart", "gear",
                         "undo", "save", "capture"};
  const std::uint16_t ink = 0x8000, tile = 0xFFFF;
  for (const char *name : names) {
    std::vector<std::uint16_t> pixels((std::size_t)deckicons::Pixels, 0x1234);
    deckicons::draw(pixels.data(), name, ink, tile);

    std::size_t drawn = 0, background = 0;
    for (std::uint16_t pixel : pixels) {
      if (pixel == ink) ++drawn;
      else if (pixel == tile) ++background;
    }
    expect(drawn + background == (std::size_t)deckicons::Pixels,
           std::string("icon ") + name + " writes only its two colours, and every pixel");
    expect(drawn > 30, std::string("icon ") + name + " draws something visible");
    expect(drawn < (std::size_t)deckicons::Pixels * 4 / 5,
           std::string("icon ") + name + " does not flood the tile");
  }

  // The configuration's generic names resolve to the specific glyph the stock
  // buttons should show, and are left alone otherwise.
  expect(std::string(deckicons::resolve("bolt", "cmd+z")) == "undo", "cmd+z shows the undo glyph");
  expect(std::string(deckicons::resolve("folder", "cmd+s")) == "save", "cmd+s shows the save glyph");
  expect(std::string(deckicons::resolve("star", "cmd+shift+4")) == "capture", "screenshot shows the capture glyph");
  expect(std::string(deckicons::resolve("bolt", "cmd+x")) == "bolt", "an unrelated action keeps its configured icon");
  expect(std::string(deckicons::resolve("music", nullptr)) == "music", "resolution tolerates a missing action");
}

void clockWraparound() {
  ContactFilter filter;
  std::uint32_t clock = 0xFFFFFFF0u;
  filter.reset(clock);
  feed(filter, true, true, 62, 80, clock, 6);  // wraps through zero
  expect(filter.pressed(), "a press spanning the millisecond counter wrap still registers");
  feed(filter, true, false, 0, 0, clock, 8);
  expect(!filter.pressed(), "the release across the wrap is recognised");
  expect(filter.contacts() == 1, "the wrapping touch counts once");
}
}  // namespace

int main() {
  transform();
  geometry();
  deliberateTap();
  repeatedTaps();
  noisePulses();
  dropoutBridging();
  missingReads();
  longHold();
  outlierRejection();
  gating();
  clockWraparound();
  icons();

  std::cout << checks << " touch checks, " << failures << " failed\n";
  return failures == 0 ? 0 : 1;
}
