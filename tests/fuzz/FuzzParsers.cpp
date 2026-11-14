// libFuzzer harness for every parser of outside input. The first byte picks
// the parser; the rest is its input. Each branch also checks what the parser
// promises about anything it accepts.
//
// Build with clang: -DPINBALL_FUZZ=ON -DPINBALL_SANITIZE=ON, then run
// build/pinball_fuzz -max_total_time=60 -dict=tests/fuzz/parsers.dict <new dir> tests/fuzz/corpus
//
// Seeds in tests/fuzz/corpus start with '0', '1', '2' or '3', which select
// the score, PNG, asset name and argument parsers in that order, with '4'
// for the review tools' window description, or with '5' for what the system
// and the player's fingers give the touch screen: a window size and safe
// area, then a run of touches (see fuzzTouchScreen), or with '6' for the
// score writer (games entered, then written and read back), '7' for a saved
// game's file as read from disk, or '8' for a saved game's records alone
// (sealed here with a good header and checksum, which the fuzzer could
// never forge, so that every field's check is reached).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/Args.hpp"
#include "core/AssetName.hpp"
#include "core/HighScores.hpp"
#include "core/InitialsEntry.hpp"
#include "core/OverlayLayout.hpp"
#include "core/PngHeader.hpp"
#include "core/Settings.hpp"
#include "core/Rules.hpp"
#include "core/Snapshot.hpp"
#include "core/SurfaceSpec.hpp"
#include "core/TouchControls.hpp"

namespace {

// Says which promise was broken before stopping, so a crash report names it.
void requireAt(bool ok, const char* what, int line) {
  if (!ok) {
    std::fprintf(stderr, "invariant broken at line %d: %s\n", line, what);
    std::abort();
  }
}
#define require(expr) requireAt((expr), #expr, __LINE__)

void fuzzHighScores(std::string_view text) {
  pinball::HighScores scores;
  if (!scores.parse(text)) {
    require(scores.size() == 0);
    return;
  }
  require(text.size() <= pinball::HighScores::kMaxTextBytes);
  require(scores.size() <= pinball::HighScores::kCapacity);
  for (std::size_t i = 0; i < scores.size(); ++i) {
    const pinball::HighScore entry = scores.at(i);
    require(entry.score > 0 && entry.score <= pinball::points::kMaxScore);
    require(i == 0 || scores.at(i - 1).score >= entry.score);
    for (const char c : entry.initials) {
      require(pinball::isInitialsChar(c));
    }
  }
  // What it writes, it reads back the same.
  const std::string written = scores.serialize();
  require(written.size() <= pinball::HighScores::kMaxTextBytes);
  pinball::HighScores again;
  require(again.parse(written));
  require(again.serialize() == written);
}

void fuzzAssetName(std::string_view name) {
  if (!pinball::isSafeAssetName(name)) {
    return;
  }
  require(!name.empty() && name.size() <= 128);
  require(!name.contains(".."));
  // Every part between slashes is a real name: not empty and not hidden.
  std::size_t start = 0;
  while (start <= name.size()) {
    std::size_t end = name.find('/', start);
    if (end == std::string_view::npos) {
      end = name.size();
    }
    const std::string_view part = name.substr(start, end - start);
    require(!part.empty() && part.front() != '.' && part.back() != '.');
    start = end + 1;
  }
  require(!name.contains('\\'));
  require(!name.contains('\0'));
  require(!name.contains(':'));
}

// The window description of the review tools (core/SurfaceSpec.hpp). The
// parser is in a header and is given its presets, so it needs nothing that
// is not already linked here.
void fuzzSurfaceSpec(std::string_view text) {
  namespace screen = pinball::screen;
  static constexpr screen::Preset kKnown[] = {
      {"desktop", {1280.0F, 960.0F, 0.0F, 0.0F, 0.0F, 0.0F}, screen::Idiom::Desktop},
      {"phone", {393.0F, 852.0F, 59.0F, 34.0F, 0.0F, 0.0F}, screen::Idiom::Touch},
  };
  const auto spec = screen::parseSurfaceSpec(text, kKnown);
  if (!spec) {
    return;
  }
  require(!text.empty() && text.size() <= 64);
  require(spec->scale >= 0 && spec->scale <= static_cast<int>(screen::kMaxSpecScale));
  const screen::Surface& s = spec->surface;
  require(s.width >= static_cast<float>(screen::kMinSpecPoints) &&
          s.width <= static_cast<float>(screen::kMaxSpecPoints));
  require(s.height >= static_cast<float>(screen::kMinSpecPoints) &&
          s.height <= static_cast<float>(screen::kMaxSpecPoints));
  require(s.safeTop >= 0.0F && s.safeBottom >= 0.0F && s.safeLeft >= 0.0F && s.safeRight >= 0.0F);
  require(s.safeTop + s.safeBottom <= s.height * screen::kMaxInsetShare);
  require(s.safeLeft + s.safeRight <= s.width * screen::kMaxInsetShare);
  // Only these characters can be part of one.
  for (const char c : text) {
    require((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || c == 'x' || c == '@' ||
            c == '+' || c == ',' || c == '-');
  }
}

// Reads a float from the next four bytes, or 0 when they run out. Any bit
// pattern: NaN, infinities, denormals and negative zero included.
float takeFloat(std::string_view& bytes) {
  float value = 0.0F;
  if (bytes.size() >= sizeof value) {
    std::memcpy(&value, bytes.data(), sizeof value);
  }
  bytes.remove_prefix(std::min(bytes.size(), sizeof value));
  return value;
}

std::uint8_t takeByte(std::string_view& bytes) {
  if (bytes.empty()) {
    return 0;
  }
  const auto value = static_cast<std::uint8_t>(bytes.front());
  bytes.remove_prefix(1);
  return value;
}

bool finiteRect(const pinball::screen::Rect& r) {
  return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.w) && std::isfinite(r.h) &&
         r.w >= 0.0F && r.h >= 0.0F;
}

// The touch screen's inputs as the iOS application receives them. The
// window's size and safe area come from the system and are believed only
// so far (core/ScreenLayout); finger positions are fractions of the window
// that SDL does not promise to keep in 0..1. The layout that results must be
// finite and drawable, and the touch state machines must stay consistent
// whatever order fingers go down, move, lift and are cancelled in.
void fuzzTouchScreen(std::string_view bytes) {
  namespace screen = pinball::screen;
  screen::Surface surface{};
  surface.width = takeFloat(bytes);
  surface.height = takeFloat(bytes);
  surface.safeTop = takeFloat(bytes);
  surface.safeBottom = takeFloat(bytes);
  surface.safeLeft = takeFloat(bytes);
  surface.safeRight = takeFloat(bytes);
  const screen::Layout layout = screen::computeLayout(surface, screen::Idiom::Touch);
  require(std::isfinite(layout.canvasWidth) && layout.canvasWidth > 0.0F);
  require(std::isfinite(layout.canvasHeight) && layout.canvasHeight > 0.0F);
  // The application hands the canvas to SDL as whole numbers (App.cpp).
  require(layout.canvasWidth < static_cast<float>(std::numeric_limits<int>::max() / 2));
  require(layout.canvasHeight < static_cast<float>(std::numeric_limits<int>::max() / 2));
  require(std::isfinite(layout.pointsPerUnit) && layout.pointsPerUnit > 0.0F);
  for (const screen::Rect& r :
       {layout.safe, layout.playfield, layout.hud, layout.deck, layout.dialog, layout.pauseButton,
        layout.nudgeLeftButton, layout.centreButton, layout.nudgeRightButton,
        layout.flipperLeftZone, layout.flipperRightZone}) {
    require(finiteRect(r));
  }
  const screen::MenuLayout menu = screen::menuLayout(layout);
  const screen::AboutLayout about = screen::aboutLayout(layout);
  const screen::InitialsLayout initials = screen::initialsLayout(layout);
  for (const screen::Rect& r : {menu.box, menu.resume, menu.newGame, menu.sound, menu.music,
                                menu.about, about.box, about.close, about.body, about.privacyLink,
                                about.previous, about.next, initials.box, initials.ok}) {
    require(finiteRect(r));
  }

  pinball::TouchControls touch;
  pinball::OverlayTaps taps;
  pinball::InitialsEntry entry;
  std::uint64_t now = 0;
  // Ids that went down and have not lifted, been cancelled or been forgotten.
  std::vector<std::uint64_t> live;
  const auto anyHeld = [](const pinball::InputState& s) {
    return s.leftFlipper || s.rightFlipper || s.plunger || s.nudgeLeft || s.nudgeRight ||
           s.nudgeUp || s.newGame;
  };
  while (!bytes.empty()) {
    const std::uint8_t control = takeByte(bytes);
    // Ten fingers' worth of ids plus a few far apart, so that both the
    // slot table and its overflow are reached.
    const std::uint8_t idByte = takeByte(bytes);
    const std::uint64_t id = (idByte & 0x80U) != 0 ? std::uint64_t{idByte} << 56U : idByte % 12U;
    const auto phase = static_cast<pinball::TouchPhase>(control & 3U);
    const float fx = takeFloat(bytes);
    const float fy = takeFloat(bytes);
    const pinball::CanvasPoint at = pinball::canvasPoint(layout, fx, fy);
    now += takeByte(bytes) * 8U;
    const pinball::TouchControls::Context context{(control & 4U) != 0, (control & 8U) != 0};

    switch ((control >> 4U) & 3U) {
      case 0: {
        const pinball::InputState before = touch.held();
        touch.handle({.id = id, .phase = phase, .x = at.x, .y = at.y}, layout, context);
        const pinball::InputState held = touch.held();
        if (phase != pinball::TouchPhase::Move) {
          std::erase(live, id);
        }
        if (phase == pinball::TouchPhase::Down) {
          live.push_back(id);
        }
        // Nothing is held once every finger is up; held never grows on a
        // Move, Up or Cancel; a control that became held was latched.
        if (live.empty()) {
          require(!anyHeld(held) && !touch.view(context).pause);
        }
        const pinball::InputState pressed = touch.takePressed();
        if (phase != pinball::TouchPhase::Down) {
          require(!anyHeld(pressed));
        }
        require(!(held.leftFlipper && !before.leftFlipper) || pressed.leftFlipper);
        require(!(held.rightFlipper && !before.rightFlipper) || pressed.rightFlipper);
        require(!(held.plunger && !before.plunger) || pressed.plunger);
        const pinball::TouchControls::View view = touch.view(context);
        require(view.left == held.leftFlipper && view.right == held.rightFlipper);
        require(view.nudgeLeft == held.nudgeLeft && view.nudgeRight == held.nudgeRight);
        (void)touch.takePauseRequest();
        break;
      }
      case 1:
        (void)taps.handle(phase, id, pinball::menuTarget(menu, at.x, at.y), now);
        break;
      case 2: {
        const pinball::OverlayTarget under = pinball::aboutTarget(about, at.x, at.y);
        if (under.kind == pinball::OverlayTarget::Kind::AboutTab) {
          require(under.index < about.tabs.size());
        }
        const pinball::OverlayTarget acted = taps.handle(phase, id, under, now);
        if (acted.kind == pinball::OverlayTarget::Kind::AboutTab) {
          require(acted.index < about.tabs.size());
        }
        break;
      }
      default: {
        const pinball::OverlayTarget under = pinball::initialsTarget(initials, at.x, at.y);
        if (under.kind == pinball::OverlayTarget::Kind::InitialUp ||
            under.kind == pinball::OverlayTarget::Kind::InitialDown ||
            under.kind == pinball::OverlayTarget::Kind::InitialSlot) {
          require(under.index < initials.slot.size());
        }
        const pinball::OverlayTarget acted = taps.handle(phase, id, under, now);
        const pinball::OverlayTarget repeated = taps.repeat(now);
        require(repeated.kind == pinball::OverlayTarget::Kind::None ||
                repeated.kind == pinball::OverlayTarget::Kind::InitialUp ||
                repeated.kind == pinball::OverlayTarget::Kind::InitialDown);
        for (const pinball::OverlayTarget& t : {acted, repeated}) {
          if (t.kind == pinball::OverlayTarget::Kind::InitialUp) {
            entry.next();
          } else if (t.kind == pinball::OverlayTarget::Kind::InitialDown) {
            entry.previous();
          } else if (t.kind == pinball::OverlayTarget::Kind::InitialSlot) {
            entry.right();
          }
        }
        require(entry.cursor() < entry.letters().size());
        for (const char c : entry.letters()) {
          require(c >= 'A' && c <= 'Z');
        }
        break;
      }
    }
    if ((control & 0xC0U) == 0xC0U && (idByte & 1U) != 0) {
      // A change of screen or layout lets every finger go (App).
      touch.cancelAll();
      taps.cancel();
      live.clear();
      const pinball::InputState held = touch.held();
      require(!held.leftFlipper && !held.rightFlipper && !held.plunger && !held.nudgeLeft &&
              !held.nudgeRight && !held.nudgeUp && !held.newGame);
      require(taps.pressed().kind == pinball::OverlayTarget::Kind::None);
    }
  }
}

// The score writer: whatever games are entered, in any order and with any
// score, rank and initials, the table it writes is one the reader accepts
// and reads back the same.
void fuzzScoreWriter(std::string_view bytes) {
  pinball::HighScores scores;
  while (bytes.size() >= 12) {
    pinball::HighScore entry;
    std::memcpy(&entry.score, bytes.data(), sizeof entry.score);
    entry.rank = static_cast<std::uint8_t>(bytes[8]);
    entry.initials = {bytes[9], bytes[10], bytes[11]};
    bytes.remove_prefix(12);
    (void)scores.submit(entry);
  }
  require(scores.size() <= pinball::HighScores::kCapacity);
  const std::string written = scores.serialize();
  require(written.size() <= pinball::HighScores::kMaxTextBytes);
  pinball::HighScores read;
  require(read.parse(written));
  require(read.size() == scores.size());
  for (std::size_t i = 0; i < scores.size(); ++i) {
    const pinball::HighScore a = scores.at(i);
    const pinball::HighScore b = read.at(i);
    require(a.score == b.score && a.rank == b.rank && a.initials == b.initials);
  }
  require(read.serialize() == written);
}

// What the saved-game reader promises about anything it accepts: it is
// valid, and it is the one way to write that game (so nothing the writer
// would not have written gets through).
void checkSavedGame(const pinball::save::Snapshot& snapshot) {
  namespace save = pinball::save;
  require(save::valid(snapshot));
  const std::string payload = save::encodePayload(snapshot);
  require(payload.size() == save::payloadSize());
  const auto again = save::decodePayload(std::span(
      reinterpret_cast<const std::uint8_t*>(payload.data()), payload.size()));
  require(again.has_value() && *again == snapshot);
  require(snapshot.rules.ball >= 1 && snapshot.rules.ball <= pinball::Rules::kBallsPerGame);
  std::size_t active = 0;
  for (const save::BallRecord& ball : snapshot.balls) {
    active += ball.active ? 1U : 0U;
    require(ball.lockBay <= pinball::table::kLockBays.size());
    require(pinball::table::isValid(ball.holder) ||
            ball.holder.kind == pinball::table::ElementKind::None);
    require(std::isfinite(ball.x) && std::isfinite(ball.y) && std::isfinite(ball.vx) &&
            std::isfinite(ball.vy));
  }
  require(active >= 1 && active + snapshot.game.queuedBalls <= pinball::table::kMaxBalls);
}

void fuzzSavedGameFile(std::string_view bytes) {
  namespace save = pinball::save;
  const std::span<const std::uint8_t> input(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                            bytes.size());
  const auto snapshot = save::decode(input);
  if (!snapshot) {
    return;
  }
  require(bytes.size() == save::fileSize());
  checkSavedGame(*snapshot);
  require(save::encode(*snapshot) == bytes);
}

// A good set of records to lay the fuzzer's bytes over: one ball resting on
// the plunger of a game just begun.
const std::string& goodRecords() {
  static const std::string records = [] {
    namespace save = pinball::save;
    save::Snapshot snapshot;
    for (std::size_t i = 0; i < snapshot.table.flipperAngles.size(); ++i) {
      snapshot.table.flipperAngles[i] = pinball::table::kFlippers[i].restAngle;
    }
    save::BallRecord& ball = snapshot.balls[0];
    ball.active = true;
    ball.x = static_cast<double>(pinball::table::kBallStart.x / save::kPixelsPerMeter);
    ball.y = static_cast<double>(pinball::table::kBallStart.y / save::kPixelsPerMeter);
    ball.holeArmed.fill(true);
    ball.searchAnchor = pinball::table::kBallStart;
    require(save::valid(snapshot));
    return save::encodePayload(snapshot);
  }();
  return records;
}

void fuzzSavedGameRecords(std::string_view input) {
  namespace save = pinball::save;
  // A whole set of records as given; anything else is a patch: two bytes of
  // offset, then bytes laid over good records there. So every field can be
  // reached by an input far shorter than the records.
  std::string records = goodRecords();
  if (input.size() == records.size()) {
    records.assign(input);
  } else if (input.size() >= 2) {
    const std::size_t offset = (static_cast<std::size_t>(static_cast<std::uint8_t>(input[0])) |
                                (static_cast<std::size_t>(static_cast<std::uint8_t>(input[1]))
                                 << 8U)) %
                               records.size();
    const std::string_view patch = input.substr(2);
    const std::size_t count = std::min(patch.size(), records.size() - offset);
    records.replace(offset, count, patch.substr(0, count));
  }
  const std::string_view bytes = records;
  const auto snapshot = save::decodePayload(
      std::span(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size()));
  // The same bytes in a sealed file read the same.
  const std::string sealed = [&] {
    save::Snapshot blank;
    std::string file = save::encode(blank);
    const std::size_t headerSize = file.size() - save::payloadSize() - 32;
    file.resize(headerSize);
    file.append(bytes.data(), bytes.size());
    const auto sum = pinball::sha256(reinterpret_cast<const std::uint8_t*>(file.data()),
                                     file.size());
    file.append(reinterpret_cast<const char*>(sum.data()), sum.size());
    return file;
  }();
  const auto fromFile = save::decode(
      std::span(reinterpret_cast<const std::uint8_t*>(sealed.data()), sealed.size()));
  require(snapshot.has_value() == fromFile.has_value());
  if (!snapshot) {
    return;
  }
  require(bytes.size() == save::payloadSize());
  require(*snapshot == *fromFile);
  checkSavedGame(*snapshot);
  require(save::encodePayload(*snapshot) == bytes);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const std::uint8_t which = data[0] % 4U;
  const std::uint8_t* const body = data + 1;
  const std::size_t length = size - 1;
  // A copy of exactly the input's size, so any read past it is caught.
  const std::string text(reinterpret_cast<const char*>(body), length);
  if (data[0] == '4') {
    fuzzSurfaceSpec(text);
    return 0;
  }
  if (data[0] == '5') {
    fuzzTouchScreen(text);
    return 0;
  }
  if (data[0] == '6') {
    fuzzScoreWriter(text);
    return 0;
  }
  if (data[0] == '7') {
    fuzzSavedGameFile(text);
    return 0;
  }
  if (data[0] == '8') {
    fuzzSavedGameRecords(text);
    return 0;
  }
  if (data[0] == '9') {
    // The settings file: anything accepted is in range and comes back the
    // same; anything refused leaves the defaults.
    pinball::Settings settings;
    if (settings.parse(text)) {
      require(settings.volume <= pinball::Settings::kMaxVolume &&
              settings.gamesFinished <= pinball::Settings::kMaxGames);
      pinball::Settings again;
      require(again.parse(settings.serialize()) && again == settings);
    } else {
      require(settings == pinball::Settings{});
    }
    return 0;
  }
  switch (which) {
    case 0:
      fuzzHighScores(text);
      break;
    case 1: {
      const auto png = pinball::parsePngHeader(
          std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
      require(!png || (text.size() >= pinball::kPngHeaderBytes && png->width > 0 &&
                       png->height > 0));
      break;
    }
    case 2:
      fuzzAssetName(text);
      break;
    default: {
      const auto frames = pinball::parseFrameLimit(text);
      require(!frames || (*frames >= 1 && *frames <= pinball::kMaxFrameLimit));
      // The same text as a scene name: only a listed name is one, and it
      // names itself back.
      const auto scene = pinball::parseScene(text);
      require(!scene || pinball::sceneName(*scene) == text);
      break;
    }
  }
  return 0;
}
