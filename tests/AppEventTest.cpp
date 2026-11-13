// The application's handling of events, driven as SDL would drive it: keys,
// fingers, window changes and the life cycle of a mobile application. Runs
// the real App on SDL's dummy video and audio drivers, with no window on
// any screen, no sound and no score file.
#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>

#include "Check.hpp"
#include "core/OverlayLayout.hpp"
#include "core/ScreenLayout.hpp"
#include "core/TouchControls.hpp"
#include "platform/About.hpp"
#include "platform/App.hpp"
#include "platform/AudioOutput.hpp"

namespace {

using pinball::App;
using pinball::AppOptions;
using pinball::FrameResult;
using pinball::screen::Rect;

// Over the environment too, which the test runner may have set.
void setAudioDriver(const char* name) {
  SDL_SetHintWithPriority(SDL_HINT_AUDIO_DRIVER, name, SDL_HINT_OVERRIDE);
}

// Eight simulation steps for every frame: the most a frame may run.
constexpr double kFastFrame = 8.0 / 120.0;

AppOptions playerOptions(pinball::screen::Idiom idiom) {
  AppOptions options;
  options.idiom = idiom;
  options.keepScores = false;
  options.fixedFrameSeconds = kFastFrame;
  return options;
}

void key(App& app, SDL_Keycode code, SDL_Scancode scancode, SDL_Keymod mod = SDL_KMOD_NONE,
         bool repeat = false) {
  SDL_Event event{};
  event.key.type = SDL_EVENT_KEY_DOWN;
  event.key.key = code;
  event.key.scancode = scancode;
  event.key.mod = mod;
  event.key.down = true;
  event.key.repeat = repeat;
  app.handleEvent(event);
}

void system(App& app, SDL_EventType type) {
  SDL_Event event{};
  event.type = type;
  app.handleEvent(event);
}

struct Point {
  float x;
  float y;
};

Point centreOf(const Rect& r) { return {r.x + r.w * 0.5F, r.y + r.h * 0.5F}; }

// A finger event at a place on the canvas, as SDL reports it: a fraction of
// the window.
void finger(App& app, SDL_EventType type, SDL_FingerID id, Point canvas) {
  const pinball::screen::Layout layout = app.status().layout;
  SDL_Event event{};
  event.tfinger.type = type;
  event.tfinger.touchID = 1;
  event.tfinger.fingerID = id;
  event.tfinger.x = canvas.x / layout.canvasWidth;
  event.tfinger.y = canvas.y / layout.canvasHeight;
  app.handleEvent(event);
}

void tap(App& app, Point canvas, SDL_FingerID id = 50) {
  finger(app, SDL_EVENT_FINGER_DOWN, id, canvas);
  finger(app, SDL_EVENT_FINGER_UP, id, canvas);
}

bool frames(App& app, int count) {
  for (int i = 0; i < count; ++i) {
    if (app.frame() != FrameResult::Continue) {
      return false;
    }
  }
  return true;
}

Point flipperPoint(const pinball::screen::Layout& layout, bool left) {
  const Rect& zone = left ? layout.flipperLeftZone : layout.flipperRightZone;
  return {left ? zone.x + 4.0F : zone.x + zone.w - 4.0F, zone.y + zone.h - 4.0F};
}

// Plays balls without flipping until the game is over. hold and release
// work the plunger. False if the game would not end.
template <typename Hold, typename Release>
bool playToGameOver(App& app, Hold hold, Release release) {
  for (int round = 0; round < 400; ++round) {
    for (int i = 0; i < 12; ++i) {
      hold();
      if (!frames(app, 1)) {
        return false;
      }
    }
    release();
    for (int i = 0; i < 40; ++i) {
      if (!frames(app, 1)) {
        return false;
      }
      const App::Status status = app.status();
      if (status.gameOver || status.entryOpen) {
        return true;
      }
    }
  }
  return false;
}

void testDesktopKeys() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Desktop));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 5));
  App::Status status = app->status();
  CHECK(status.layout.idiom == pinball::screen::Idiom::Desktop);
  CHECK(status.layout.canvasWidth == 1280.0F && status.layout.canvasHeight == 960.0F);
  CHECK(!status.paused && !status.aboutOpen && !status.entryOpen && !status.interrupted);
  CHECK(status.steps > 0);
  CHECK(status.drawnFrames == status.frames);

  // F3 pauses: no more steps. Key repeat does not toggle it back.
  key(*app, SDLK_F3, SDL_SCANCODE_F3);
  key(*app, SDLK_F3, SDL_SCANCODE_F3, SDL_KMOD_NONE, true);
  CHECK(app->status().paused);
  CHECK(frames(*app, 3));
  const std::uint64_t stepsAtPause = app->status().steps;
  const std::uint32_t drawnAtPause = app->status().drawnFrames;
  CHECK(frames(*app, 20));
  status = app->status();
  CHECK(status.steps == stepsAtPause);
  // A loop that cannot sleep does not redraw the standing picture.
  CHECK(status.drawnFrames == drawnAtPause);
  CHECK(status.frames >= 28);
  // An event may have changed it: drawn once more, then still again.
  key(*app, SDLK_M, SDL_SCANCODE_M);
  CHECK(app->status().muted);
  CHECK(frames(*app, 5));
  CHECK(app->status().drawnFrames == drawnAtPause + 1);
  key(*app, SDLK_M, SDL_SCANCODE_M);
  CHECK(!app->status().muted);
  key(*app, SDLK_N, SDL_SCANCODE_N);
  CHECK(!app->status().musicOn);
  key(*app, SDLK_N, SDL_SCANCODE_N);
  CHECK(app->status().musicOn);
  // A control pressed during the pause does not reach the game after it.
  key(*app, SDLK_F2, SDL_SCANCODE_F2);
  key(*app, SDLK_F3, SDL_SCANCODE_F3);
  CHECK(!app->status().paused);
  CHECK(frames(*app, 3));
  status = app->status();
  CHECK(status.steps > stepsAtPause);
  CHECK(!status.input.newGame);

  // F1 opens the About screen and closes it; Escape closes it too. F3 does
  // nothing behind it. The game stands still.
  key(*app, SDLK_F1, SDL_SCANCODE_F1);
  CHECK(app->status().aboutOpen);
  CHECK(app->status().about.page == pinball::AboutPage::Scores);
  key(*app, SDLK_F3, SDL_SCANCODE_F3);
  CHECK(!app->status().paused);
  CHECK(frames(*app, 2));
  const std::uint64_t stepsAtAbout = app->status().steps;
  CHECK(frames(*app, 5));
  CHECK(app->status().steps == stepsAtAbout);
  key(*app, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
  CHECK(!app->status().aboutOpen);
  key(*app, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
  CHECK(!app->status().aboutOpen);
  key(*app, SDLK_F1, SDL_SCANCODE_F1);
  key(*app, SDLK_F1, SDL_SCANCODE_F1);
  CHECK(!app->status().aboutOpen);

  // The desktop has no on-screen controls: fingers do nothing anywhere.
  for (float y = 0.0F; y < 960.0F; y += 120.0F) {
    for (float x = 0.0F; x < 1280.0F; x += 160.0F) {
      finger(*app, SDL_EVENT_FINGER_DOWN, 3, {x, y});
      CHECK(frames(*app, 1));
      status = app->status();
      CHECK(!status.paused && !status.aboutOpen);
      CHECK(!status.input.leftFlipper && !status.input.rightFlipper && !status.input.plunger);
      finger(*app, SDL_EVENT_FINGER_UP, 3, {x, y});
    }
  }

  // A pressed control reaches the next step, and only while it is pressed.
  key(*app, SDLK_Z, SDL_SCANCODE_Z);
  CHECK(frames(*app, 1));
  CHECK(app->status().input.leftFlipper);
  CHECK(frames(*app, 1));
  CHECK(!app->status().input.leftFlipper);

  // Play the game out with the plunger alone. The score makes the (empty)
  // table, so the initials prompt opens.
  const bool over = playToGameOver(
      *app, [&app] { key(*app, SDLK_SPACE, SDL_SCANCODE_SPACE); }, [] {});
  CHECK(over);
  status = app->status();
  CHECK(status.entryOpen);
  if (!status.entryOpen) {
    return;
  }
  CHECK(status.initials == (pinball::Initials{'A', 'A', 'A'}) && status.cursor == 0);
  // In its first moments the prompt takes no keys: they are still play.
  key(*app, SDLK_Q, SDL_SCANCODE_Q);
  key(*app, SDLK_RETURN, SDL_SCANCODE_RETURN);
  CHECK(app->status().entryOpen);
  CHECK(app->status().initials[0] == 'A');
  // F1 does not open the About screen over it.
  key(*app, SDLK_F1, SDL_SCANCODE_F1);
  CHECK(!app->status().aboutOpen);
  SDL_Delay(750);
  // A shortcut is not typing.
  key(*app, SDLK_Q, SDL_SCANCODE_Q, SDL_KMOD_GUI);
  CHECK(app->status().initials[0] == 'A');
  // Letters, the arrows, backspace.
  key(*app, SDLK_Q, SDL_SCANCODE_Q);
  status = app->status();
  CHECK(status.initials[0] == 'Q' && status.cursor == 1);
  key(*app, SDLK_UP, SDL_SCANCODE_UP);
  key(*app, SDLK_UP, SDL_SCANCODE_UP);
  key(*app, SDLK_DOWN, SDL_SCANCODE_DOWN);
  CHECK(app->status().initials[1] == 'B');
  key(*app, SDLK_RIGHT, SDL_SCANCODE_RIGHT);
  key(*app, SDLK_DOWN, SDL_SCANCODE_DOWN);
  status = app->status();
  CHECK(status.cursor == 2 && status.initials[2] == 'Z');
  key(*app, SDLK_BACKSPACE, SDL_SCANCODE_BACKSPACE);
  key(*app, SDLK_LEFT, SDL_SCANCODE_LEFT);
  key(*app, SDLK_LEFT, SDL_SCANCODE_LEFT);
  CHECK(app->status().cursor == 0);
  // Space is the plunger, not Enter.
  key(*app, SDLK_SPACE, SDL_SCANCODE_SPACE);
  CHECK(app->status().entryOpen && app->status().cursor == 0);
  // The cursor blinks: a standing prompt is drawn again when it does, and
  // not for every frame.
  CHECK(frames(*app, 2));
  const std::uint32_t drawnAtPrompt = app->status().drawnFrames;
  const std::uint32_t framesAtPrompt = app->status().frames;
  for (int i = 0; i < 12; ++i) {
    SDL_Delay(100);
    CHECK(frames(*app, 3));
  }
  status = app->status();
  CHECK(status.drawnFrames > drawnAtPrompt);
  CHECK(status.drawnFrames - drawnAtPrompt < status.frames - framesAtPrompt);
  // Enter confirms a letter at a time; the third closes the prompt.
  key(*app, SDLK_RETURN, SDL_SCANCODE_RETURN);
  key(*app, SDLK_KP_ENTER, SDL_SCANCODE_KP_ENTER);
  CHECK(app->status().entryOpen && app->status().cursor == 2);
  key(*app, SDLK_RETURN, SDL_SCANCODE_RETURN);
  status = app->status();
  CHECK(!status.entryOpen && status.gameOver);

  // F2 starts another game.
  CHECK(frames(*app, 2));
  key(*app, SDLK_F2, SDL_SCANCODE_F2);
  CHECK(frames(*app, 2));
  CHECK(!app->status().gameOver);

  // Quit ends the loop, and the exit code is success.
  system(*app, SDL_EVENT_QUIT);
  CHECK(app->frame() == FrameResult::Quit);
  CHECK(app->finish() == 0);
  CHECK(app->finish() == 0);
}

void testQuitAtThePromptKeepsTheScore() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Desktop));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(playToGameOver(
      *app, [&app] { key(*app, SDLK_SPACE, SDL_SCANCODE_SPACE); }, [] {}));
  CHECK(app->status().entryOpen);
  // Escape accepts the letters as they stand (after the guard).
  SDL_Delay(750);
  key(*app, SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
  CHECK(!app->status().entryOpen);
}

void testLifecycle() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Touch));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 5));
  const pinball::screen::Layout layout = app->status().layout;
  CHECK(layout.idiom == pinball::screen::Idiom::Touch);

  // A thumb is holding a flipper when the application is covered.
  finger(*app, SDL_EVENT_FINGER_DOWN, 1, flipperPoint(layout, true));
  CHECK(frames(*app, 2));
  CHECK(app->status().input.leftFlipper && app->status().touch.left);
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  App::Status status = app->status();
  CHECK(status.paused && status.interrupted);
  CHECK(!status.touch.left);
  CHECK(!status.input.leftFlipper);
  // Not yet in the background: the paused picture is still drawn, once.
  const std::uint32_t drawnBefore = status.drawnFrames;
  CHECK(frames(*app, 3));
  CHECK(app->status().drawnFrames == drawnBefore + 1);
  const std::uint64_t stepsBefore = app->status().steps;

  // In the background nothing is drawn, whatever arrives.
  system(*app, SDL_EVENT_DID_ENTER_BACKGROUND);
  CHECK(app->status().backgrounded);
  system(*app, SDL_EVENT_LOW_MEMORY);
  CHECK(frames(*app, 5));
  status = app->status();
  CHECK(status.drawnFrames == drawnBefore + 1);
  CHECK(status.steps == stepsBefore);

  // Back: still paused, with the menu, until the player says so. The thumb
  // that never left the glass does not hold the flipper.
  system(*app, SDL_EVENT_WILL_ENTER_FOREGROUND);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  CHECK(frames(*app, 5));
  status = app->status();
  CHECK(!status.backgrounded && status.paused && status.interrupted);
  CHECK(status.steps == stepsBefore);
  CHECK(status.drawnFrames == drawnBefore + 2);
  finger(*app, SDL_EVENT_FINGER_MOTION, 1, flipperPoint(layout, true));
  finger(*app, SDL_EVENT_FINGER_UP, 1, flipperPoint(layout, true));
  CHECK(app->status().paused);
  // RESUME.
  tap(*app, centreOf(pinball::screen::menuLayout(layout).resume));
  status = app->status();
  CHECK(!status.paused && !status.interrupted);
  CHECK(frames(*app, 4));
  status = app->status();
  CHECK(status.steps > stepsBefore);
  CHECK(!status.input.leftFlipper);

  // Covered for a moment only (the control panel, a call): "will enter
  // background" and straight back. Paused all the same.
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  status = app->status();
  CHECK(status.paused && status.interrupted && !status.backgrounded);
  // A tap outside the menu resumes too.
  const Rect box = pinball::screen::menuLayout(layout).box;
  tap(*app, {box.x + box.w * 0.5F, box.y - 20.0F});
  CHECK(!app->status().paused);

  // The same events twice over change nothing more.
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  CHECK(app->status().paused && !app->status().backgrounded);
  CHECK(frames(*app, 3));

  // The system ends the application: the loop ends, cleanly.
  system(*app, SDL_EVENT_TERMINATING);
  CHECK(app->frame() == FrameResult::Quit);
  CHECK(app->finish() == 0);
}

void testBackgroundKeepsAScoreAtThePrompt() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Touch));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 3));
  const pinball::screen::Layout layout = app->status().layout;
  const Point centre = centreOf(layout.centreButton);
  CHECK(app->status().touch.centreRole == pinball::TouchControls::View::Centre::Launch);
  const bool over = playToGameOver(
      *app, [&] { finger(*app, SDL_EVENT_FINGER_DOWN, 9, centre); },
      [&] { finger(*app, SDL_EVENT_FINGER_UP, 9, centre); });
  CHECK(over);
  CHECK(app->status().entryOpen);
  // Going to the background at the prompt enters the score under the
  // letters shown: the application may never run again.
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  CHECK(app->status().entryOpen);
  system(*app, SDL_EVENT_DID_ENTER_BACKGROUND);
  App::Status status = app->status();
  CHECK(!status.entryOpen && status.paused && status.gameOver);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  CHECK(frames(*app, 2));
  // NEW GAME with the game over needs no second tap.
  tap(*app, centreOf(pinball::screen::menuLayout(layout).newGame));
  CHECK(!app->status().paused);
  CHECK(frames(*app, 3));
  CHECK(!app->status().gameOver);
}

void testTouchPlayAndMenus() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Touch));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 3));
  const pinball::screen::Layout layout = app->status().layout;
  CHECK(layout.composition == pinball::screen::Composition::Stack);
  const Point left = flipperPoint(layout, true);
  const Point right = flipperPoint(layout, false);
  using Centre = pinball::TouchControls::View::Centre;

  // Both flippers, each finger its own; a slide across does not swap.
  finger(*app, SDL_EVENT_FINGER_DOWN, 1, left);
  finger(*app, SDL_EVENT_FINGER_DOWN, 2, right);
  finger(*app, SDL_EVENT_FINGER_MOTION, 1, right);
  CHECK(frames(*app, 2));
  App::Status status = app->status();
  CHECK(status.input.leftFlipper && status.input.rightFlipper);
  finger(*app, SDL_EVENT_FINGER_UP, 1, right);
  CHECK(frames(*app, 2));
  status = app->status();
  CHECK(!status.input.leftFlipper && status.input.rightFlipper);
  // A touch the system takes away lets go.
  finger(*app, SDL_EVENT_FINGER_CANCELED, 2, right);
  CHECK(frames(*app, 2));
  CHECK(!app->status().input.rightFlipper);
  // A tap shorter than a frame is still one press.
  tap(*app, left, 3);
  CHECK(frames(*app, 1));
  CHECK(app->status().input.leftFlipper);
  CHECK(frames(*app, 1));
  CHECK(!app->status().input.leftFlipper);

  // The centre button draws the plunger while a ball waits.
  CHECK(app->status().touch.centreRole == Centre::Launch);
  finger(*app, SDL_EVENT_FINGER_DOWN, 4, centreOf(layout.centreButton));
  CHECK(frames(*app, 12));
  status = app->status();
  CHECK(status.input.plunger && !status.input.leftFlipper && !status.input.rightFlipper);
  finger(*app, SDL_EVENT_FINGER_UP, 4, centreOf(layout.centreButton));
  CHECK(frames(*app, 2));
  CHECK(!app->status().input.plunger);
  // The nudge buttons.
  finger(*app, SDL_EVENT_FINGER_DOWN, 5, centreOf(layout.nudgeLeftButton));
  CHECK(frames(*app, 1));
  CHECK(app->status().input.nudgeLeft && !app->status().input.leftFlipper);
  finger(*app, SDL_EVENT_FINGER_UP, 5, centreOf(layout.nudgeLeftButton));

  // The pause button, with a thumb on a flipper at the time.
  finger(*app, SDL_EVENT_FINGER_DOWN, 6, right);
  CHECK(frames(*app, 1));
  finger(*app, SDL_EVENT_FINGER_DOWN, 7, centreOf(layout.pauseButton));
  CHECK(!app->status().paused);
  finger(*app, SDL_EVENT_FINGER_UP, 7, centreOf(layout.pauseButton));
  status = app->status();
  CHECK(status.paused && !status.interrupted);
  CHECK(!status.touch.right);
  CHECK(frames(*app, 2));
  CHECK(!app->status().input.rightFlipper);
  const std::uint64_t stepsAtPause = app->status().steps;

  const pinball::screen::MenuLayout menu = pinball::screen::menuLayout(layout);
  // The thumb still on the glass lifts over a button: it is not a tap.
  finger(*app, SDL_EVENT_FINGER_MOTION, 6, centreOf(menu.sound));
  finger(*app, SDL_EVENT_FINGER_UP, 6, centreOf(menu.sound));
  CHECK(!app->status().muted);
  tap(*app, centreOf(menu.sound));
  CHECK(app->status().muted);
  tap(*app, centreOf(menu.sound));
  CHECK(!app->status().muted);
  tap(*app, centreOf(menu.music));
  CHECK(!app->status().musicOn);
  tap(*app, centreOf(menu.music));
  CHECK(app->status().musicOn);
  // A finger that slides off a button has not pressed it.
  finger(*app, SDL_EVENT_FINGER_DOWN, 8, centreOf(menu.sound));
  finger(*app, SDL_EVENT_FINGER_MOTION, 8, centreOf(menu.music));
  finger(*app, SDL_EVENT_FINGER_UP, 8, centreOf(menu.music));
  CHECK(!app->status().muted && app->status().musicOn);
  // A tap on the menu's own background does nothing.
  tap(*app, {menu.box.x + 1.0F, menu.box.y + 1.0F});
  CHECK(app->status().paused);

  // NEW GAME asks first; another button takes the question back.
  tap(*app, centreOf(menu.newGame));
  status = app->status();
  CHECK(status.confirmNewGame && status.paused);
  tap(*app, centreOf(menu.music));
  CHECK(!app->status().confirmNewGame);
  tap(*app, centreOf(menu.music));

  // SCORES & ABOUT: the pages, and back to the menu.
  tap(*app, centreOf(menu.about));
  status = app->status();
  CHECK(status.aboutOpen && status.paused);
  CHECK(status.about.page == pinball::AboutPage::Scores);
  const pinball::screen::AboutLayout about = pinball::screen::aboutLayout(layout);
  tap(*app, centreOf(about.tabs[2]));
  CHECK(app->status().about.page == pinball::AboutPage::Licences);
  // Paging stays inside the pages there are.
  for (std::size_t i = 0; i < pinball::licencePageCount(layout) + 2; ++i) {
    tap(*app, centreOf(about.next));
  }
  CHECK(app->status().about.licencePage == pinball::licencePageCount(layout) - 1);
  for (std::size_t i = 0; i < pinball::licencePageCount(layout) + 2; ++i) {
    tap(*app, centreOf(about.previous));
  }
  CHECK(app->status().about.licencePage == 0);
  tap(*app, centreOf(about.tabs[1]));
  CHECK(app->status().about.page == pinball::AboutPage::About);
  // The privacy button with no address published opens nothing, and the
  // screen stays.
  tap(*app, centreOf(about.privacyLink));
  CHECK(app->status().aboutOpen);
  tap(*app, centreOf(about.tabs[0]));
  CHECK(app->status().about.page == pinball::AboutPage::Scores);
  tap(*app, centreOf(about.close));
  status = app->status();
  CHECK(!status.aboutOpen && status.paused);
  // Open again: the first page again. A tap outside the box closes it.
  tap(*app, centreOf(menu.about));
  tap(*app, centreOf(about.tabs[2]));
  tap(*app, {about.box.x + about.box.w * 0.5F, about.box.y - 10.0F});
  CHECK(!app->status().aboutOpen && app->status().paused);
  tap(*app, centreOf(menu.about));
  CHECK(app->status().about.page == pinball::AboutPage::Scores);
  tap(*app, centreOf(about.close));

  // Nothing ran meanwhile.
  CHECK(frames(*app, 3));
  CHECK(app->status().steps == stepsAtPause);

  // NEW GAME, twice: a new game, and play goes on.
  tap(*app, centreOf(menu.newGame));
  tap(*app, centreOf(menu.newGame));
  status = app->status();
  CHECK(!status.paused && !status.confirmNewGame);
  CHECK(frames(*app, 3));
  status = app->status();
  CHECK(status.steps > stepsAtPause && !status.gameOver);

  // The keyboard still works on a touch screen (a tablet's keyboard).
  key(*app, SDLK_Z, SDL_SCANCODE_Z);
  CHECK(frames(*app, 1));
  CHECK(app->status().input.leftFlipper);
  key(*app, SDLK_F3, SDL_SCANCODE_F3);
  CHECK(app->status().paused);
  key(*app, SDLK_F3, SDL_SCANCODE_F3);
  CHECK(!app->status().paused);
}

void testTouchInitials() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Touch));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 3));
  const pinball::screen::Layout layout = app->status().layout;
  const Point centre = centreOf(layout.centreButton);
  const bool over = playToGameOver(
      *app, [&] { finger(*app, SDL_EVENT_FINGER_DOWN, 9, centre); },
      [&] { finger(*app, SDL_EVENT_FINGER_UP, 9, centre); });
  CHECK(over);
  App::Status status = app->status();
  CHECK(status.entryOpen);
  if (!status.entryOpen) {
    return;
  }
  const pinball::screen::InitialsLayout prompt = pinball::screen::initialsLayout(layout);
  // In its first moments the prompt takes no touches, and a finger that
  // came down then does nothing when it lifts later.
  finger(*app, SDL_EVENT_FINGER_DOWN, 20, centreOf(prompt.ok));
  tap(*app, centreOf(prompt.up[0]), 21);
  CHECK(app->status().entryOpen && app->status().initials[0] == 'A');
  SDL_Delay(750);
  finger(*app, SDL_EVENT_FINGER_UP, 20, centreOf(prompt.ok));
  CHECK(app->status().entryOpen);

  // An arrow steps its own letter, and moves the cursor there.
  tap(*app, centreOf(prompt.up[1]));
  status = app->status();
  CHECK(status.initials == (pinball::Initials{'A', 'B', 'A'}) && status.cursor == 1);
  tap(*app, centreOf(prompt.down[2]));
  status = app->status();
  CHECK(status.initials == (pinball::Initials{'A', 'B', 'Z'}) && status.cursor == 2);
  tap(*app, centreOf(prompt.down[0]));
  tap(*app, centreOf(prompt.down[0]));
  status = app->status();
  CHECK(status.initials == (pinball::Initials{'Y', 'B', 'Z'}) && status.cursor == 0);
  // A letter itself only takes the cursor.
  tap(*app, centreOf(prompt.slot[2]));
  status = app->status();
  CHECK(status.initials == (pinball::Initials{'Y', 'B', 'Z'}) && status.cursor == 2);
  // A tap beside the prompt does not dismiss it.
  tap(*app, {prompt.box.x + prompt.box.w * 0.5F, prompt.box.y - 10.0F});
  CHECK(app->status().entryOpen);

  // A held arrow repeats after a moment.
  finger(*app, SDL_EVENT_FINGER_DOWN, 30, centreOf(prompt.up[1]));
  CHECK(app->status().initials[1] == 'C');
  CHECK(frames(*app, 2));
  CHECK(app->status().initials[1] == 'C');
  SDL_Delay(pinball::OverlayTaps::kRepeatDelayMs + 30);
  CHECK(frames(*app, 1));
  CHECK(app->status().initials[1] == 'D');
  CHECK(frames(*app, 1));
  CHECK(app->status().initials[1] == 'D');
  SDL_Delay(pinball::OverlayTaps::kRepeatPeriodMs + 30);
  CHECK(frames(*app, 1));
  CHECK(app->status().initials[1] == 'E');
  finger(*app, SDL_EVENT_FINGER_UP, 30, centreOf(prompt.up[1]));
  SDL_Delay(pinball::OverlayTaps::kRepeatPeriodMs + 30);
  CHECK(frames(*app, 2));
  CHECK(app->status().initials == (pinball::Initials{'Y', 'E', 'Z'}));

  // OK enters the score; the game is over behind it.
  tap(*app, centreOf(prompt.ok));
  status = app->status();
  CHECK(!status.entryOpen && status.gameOver && !status.paused);
  using Centre = pinball::TouchControls::View::Centre;
  CHECK(status.touch.centreRole == Centre::Play);
  // The centre button now starts a new game.
  CHECK(frames(*app, 2));
  tap(*app, centre);
  CHECK(frames(*app, 3));
  status = app->status();
  CHECK(!status.gameOver);
  CHECK(status.touch.centreRole == Centre::Launch);
  // The initials are offered again next time.
  const bool again = playToGameOver(
      *app, [&] { finger(*app, SDL_EVENT_FINGER_DOWN, 9, centre); },
      [&] { finger(*app, SDL_EVENT_FINGER_UP, 9, centre); });
  CHECK(again);
  if (app->status().entryOpen) {
    CHECK(app->status().initials == (pinball::Initials{'Y', 'E', 'Z'}));
  }
}

void testWindowChanges() {
  const std::unique_ptr<App> app = App::create(playerOptions(pinball::screen::Idiom::Touch));
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 3));
  // The application's one window. The list is ours to free.
  SDL_Window** const windows = SDL_GetWindows(nullptr);
  SDL_Window* const window = windows != nullptr ? windows[0] : nullptr;
  SDL_free(windows);
  CHECK(window != nullptr);
  if (window == nullptr) {
    return;
  }
  pinball::screen::Layout layout = app->status().layout;
  CHECK(layout.composition == pinball::screen::Composition::Stack);
  const float canvasBefore = layout.canvasHeight;

  // The window changes size with a thumb down, the composition staying the
  // same: the thumb lets go, the layout follows, the game goes on.
  finger(*app, SDL_EVENT_FINGER_DOWN, 1, flipperPoint(layout, true));
  CHECK(frames(*app, 2));
  CHECK(app->status().input.leftFlipper);
  CHECK(SDL_SetWindowSize(window, 430, 932));
  SDL_Event resized{};
  resized.window.type = SDL_EVENT_WINDOW_RESIZED;
  resized.window.windowID = SDL_GetWindowID(window);
  app->handleEvent(resized);
  App::Status status = app->status();
  CHECK(status.layout.canvasHeight != canvasBefore);
  CHECK(status.layout.composition == pinball::screen::Composition::Stack);
  CHECK(!status.touch.left && !status.paused);
  CHECK(frames(*app, 2));
  CHECK(!app->status().input.leftFlipper);
  // The old thumb moving and lifting does nothing; a new one works, placed
  // by the new layout.
  layout = app->status().layout;
  finger(*app, SDL_EVENT_FINGER_MOTION, 1, flipperPoint(layout, false));
  finger(*app, SDL_EVENT_FINGER_UP, 1, centreOf(layout.pauseButton));
  CHECK(!app->status().paused);
  finger(*app, SDL_EVENT_FINGER_DOWN, 2, flipperPoint(layout, false));
  CHECK(frames(*app, 1));
  CHECK(app->status().input.rightFlipper && !app->status().input.leftFlipper);

  // Turned on its side: another composition. The table has moved from
  // under the hands, so the game pauses.
  CHECK(SDL_SetWindowSize(window, 1194, 834));
  app->handleEvent(resized);
  status = app->status();
  CHECK(status.layout.composition == pinball::screen::Composition::Classic);
  CHECK(status.paused && status.interrupted && !status.touch.right);
  CHECK(frames(*app, 2));
  // The menu is where the new layout puts it.
  layout = app->status().layout;
  tap(*app, centreOf(pinball::screen::menuLayout(layout).resume));
  CHECK(!app->status().paused);
  // Safe-area and pixel-size changes go the same way and are harmless
  // when nothing has changed.
  resized.window.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
  app->handleEvent(resized);
  resized.window.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
  app->handleEvent(resized);
  CHECK(!app->status().paused);
  CHECK(app->status().layout.composition == pinball::screen::Composition::Classic);
  CHECK(frames(*app, 3));
}

void testTimedRun() {
  // A timed run draws every frame, is never paused by the system, and ends
  // by itself. In the touch idiom too, as on a device.
  AppOptions options;
  options.idiom = pinball::screen::Idiom::Touch;
  options.frameLimit = 40;
  options.backgroundAudioOpen = true;
  const std::unique_ptr<App> app = App::create(options);
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(frames(*app, 10));
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  CHECK(!app->status().paused);
  int count = 10;
  while (app->frame() == FrameResult::Continue && count < 1000) {
    ++count;
    SDL_Delay(2);
  }
  CHECK(count == 39);
  const App::Status status = app->status();
  CHECK(status.frames == 40 && status.drawnFrames == 40);
  CHECK(status.steps > 0);
  CHECK(app->finish() == 0);
}

void testBackgroundAudioOpen() {
  // The opener by itself: the device arrives a little later, once.
  {
    pinball::AudioOpener opener;
    CHECK(!opener.busy());
    CHECK(opener.take() == nullptr);
    opener.start();
    opener.start();
    std::unique_ptr<pinball::AudioOutput> device;
    for (int i = 0; i < 400 && !device; ++i) {
      device = opener.take();
      if (!device) {
        SDL_Delay(5);
      }
    }
    CHECK(device != nullptr);
    CHECK(!opener.busy());
    CHECK(opener.take() == nullptr);
    if (device) {
      pinball::SoundMixer mixer;
      device->pump(mixer);
      device->suspend();
      device->pump(mixer);
      device->resume();
      device->pump(mixer);
    }
  }
  // Started and never taken: closed by the destructor, which waits for it.
  {
    pinball::AudioOpener opener;
    opener.start();
  }
  // With no audio at all the opener just has nothing to give.
  {
    setAudioDriver("no-such-driver");
    pinball::AudioOpener opener;
    opener.start();
    SDL_Delay(20);
    CHECK(opener.take() == nullptr);
    CHECK(!opener.busy());
    setAudioDriver("dummy");
  }

  // In the application: silent for the first frames, then the device.
  AppOptions options = playerOptions(pinball::screen::Idiom::Touch);
  options.backgroundAudioOpen = true;
  const std::unique_ptr<App> app = App::create(options);
  CHECK(app != nullptr);
  if (!app) {
    return;
  }
  CHECK(!app->status().audioOpen);
  CHECK(frames(*app, 2));
  CHECK(!app->status().audioOpen);
  for (int i = 0; i < 400 && !app->status().audioOpen; ++i) {
    CHECK(frames(*app, 1));
    SDL_Delay(5);
  }
  CHECK(app->status().audioOpen);
  // Through the background and back it is still there.
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_BACKGROUND);
  CHECK(frames(*app, 2));
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  CHECK(frames(*app, 2));
  CHECK(app->status().audioOpen);
}

void testNoAudioRetriesOnReturn() {
  // No device at launch (as in a simulator with no audio): the game runs
  // silent, and tries again when it comes back to the foreground.
  setAudioDriver("no-such-driver");
  AppOptions options = playerOptions(pinball::screen::Idiom::Touch);
  options.backgroundAudioOpen = true;
  const std::unique_ptr<App> app = App::create(options);
  CHECK(app != nullptr);
  if (!app) {
    setAudioDriver("dummy");
    return;
  }
  CHECK(frames(*app, 30));
  CHECK(!app->status().audioOpen);
  CHECK(app->status().steps > 0);
  // Audio becomes possible. Nothing retries by itself...
  setAudioDriver("dummy");
  CHECK(frames(*app, 30));
  CHECK(!app->status().audioOpen);
  // ...until the application returns to the foreground; then, once the
  // waiting time between attempts has passed, it opens.
  system(*app, SDL_EVENT_WILL_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_BACKGROUND);
  system(*app, SDL_EVENT_DID_ENTER_FOREGROUND);
  CHECK(frames(*app, 5));
  CHECK(!app->status().audioOpen);
  for (int i = 0; i < 700 && !app->status().audioOpen; ++i) {
    SDL_Delay(10);
    CHECK(frames(*app, 1));
  }
  CHECK(app->status().audioOpen);
}

void runAll() {
  testDesktopKeys();
  testQuitAtThePromptKeepsTheScore();
  testLifecycle();
  testBackgroundKeepsAScoreAtThePrompt();
  testTouchPlayAndMenus();
  testTouchInitials();
  testWindowChanges();
  testTimedRun();
  testBackgroundAudioOpen();
  testNoAudioRetriesOnReturn();
}

}  // namespace

int main() {
  // No window on a screen, no sound, whatever the environment says.
  SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, "dummy", SDL_HINT_OVERRIDE);
  setAudioDriver("dummy");
  try {
    runAll();
  } catch (const std::exception& e) {
    std::fprintf(stderr, "app_tests: uncaught exception: %s\n", e.what());
    return EXIT_FAILURE;
  } catch (...) {
    std::fputs("app_tests: uncaught exception\n", stderr);
    return EXIT_FAILURE;
  }
  SDL_Quit();
  if (pinball_test::failures() != 0) {
    std::fprintf(stderr, "app_tests: %d failure(s)\n", pinball_test::failures());
    return EXIT_FAILURE;
  }
  if (pinball_test::checks() == 0) {
    std::fputs("app_tests: no checks ran\n", stderr);
    return EXIT_FAILURE;
  }
  std::printf("app_tests: all %ld checks passed\n", pinball_test::checks());
  return EXIT_SUCCESS;
}
