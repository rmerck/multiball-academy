// Compiled for iOS only (CMakeLists.txt), as Objective-C++ with ARC. The
// Objective-C of the game on this system: three questions and a count,
// haptics (Core Haptics, with UIKit's generators where it is missing), the
// review request (whose API is Swift's: ios/Review.swift) and what
// VoiceOver finds on the menu screens.
#include "platform/ios/IosSystem.hpp"

#import <AVFAudio/AVFAudio.h>
#import <CoreHaptics/CoreHaptics.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <os/log.h>

#include <SDL3/SDL.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>

// In ios/Review.swift.
extern "C" void pinball_request_review(void);

// A control of a menu screen as VoiceOver sees it. Activating it hands the
// game an SDL event, so it acts on its own loop like any other input.
@interface PinballAccessibleElement : UIAccessibilityElement
@property(nonatomic) Uint32 eventType;
@property(nonatomic) Sint32 code;
@end

@implementation PinballAccessibleElement
- (BOOL)accessibilityActivate {
  if (self.eventType == 0) {
    return NO;
  }
  SDL_Event event;
  SDL_zero(event);
  event.type = self.eventType;
  event.user.code = self.code;
  return SDL_PushEvent(&event) ? YES : NO;
}
@end

namespace pinball::ios {

namespace {

// Raised on whatever thread the notification arrives on, read on the main
// thread.
std::atomic<unsigned> gRouteChanges{0};

}  // namespace

bool reduceMotionEnabled() noexcept {
  @autoreleasepool {
    return UIAccessibilityIsReduceMotionEnabled() ? true : false;
  }
}

bool otherAudioPlaying() noexcept {
  @autoreleasepool {
    // A question only: the session is neither configured nor activated
    // here. SDL does both when the device is opened.
    return [AVAudioSession sharedInstance].otherAudioPlaying ? true : false;
  }
}

unsigned audioRouteChanges() noexcept {
  // The observer is registered once and lives as long as the process, as
  // the notification centre keeps it.
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    @autoreleasepool {
      [[NSNotificationCenter defaultCenter]
          addObserverForName:AVAudioSessionRouteChangeNotification
                      object:nil
                       queue:nil
                  usingBlock:^(NSNotification* note) {
                    // Only a device that came or went. The session's own
                    // changes of category and configuration, which opening
                    // the device causes, are route changes too, and must
                    // not make the game open it again.
                    NSNumber* reason = note.userInfo[AVAudioSessionRouteChangeReasonKey];
                    const NSUInteger value = reason.unsignedIntegerValue;
                    if (value == AVAudioSessionRouteChangeReasonNewDeviceAvailable ||
                        value == AVAudioSessionRouteChangeReasonOldDeviceUnavailable) {
                      gRouteChanges.fetch_add(1, std::memory_order_relaxed);
                    }
                  }];
    }
  });
  return gRouteChanges.load(std::memory_order_relaxed);
}

namespace {

// The game's view (SDL's): the accessibility elements belong to it.
__weak UIView* gView = nil;

// --- Haptics -----------------------------------------------------------------
//
// Core Haptics first: one engine for the application, started when the
// application becomes active and again whenever the system stops or resets
// it, playing patterns whose intensity and sharpness the game sets itself
// (core/Feedback.cpp). Where the hardware has no Core Haptics, or the
// engine cannot be made or started, UIKit's feedback generators instead,
// made without a view (a generator attached to a view plays only through
// that view's interaction) and prepared again whenever a window becomes
// key. Every call here is on the main thread, where SDL runs the game's
// loop; the engine's handlers come on threads of their own and hand their
// work to the main queue.

os_log_t hapticsLog() {
  static os_log_t log = os_log_create("io.github.rmerck.multiballacademy", "haptics");
  return log;
}

const char* kindName(Haptic kind) {
  switch (kind) {
    case Haptic::Flipper:
      return "flipper";
    case Haptic::Hit:
      return "hit";
    case Haptic::Drain:
      return "drain";
    case Haptic::Jackpot:
      return "jackpot";
  }
  return "?";
}

// None until set up; Off where the device has no haptics (an iPad).
enum class HapticPath : std::uint8_t { None, Off, Engine, Generators };

HapticPath gPath = HapticPath::None;
CHHapticEngine* gEngine = nil;
// One pattern for each kind, in the order of Haptic.
NSArray<CHHapticPattern*>* gPatterns = nil;
// Set by the start and cleared by the stopped and reset handlers, which
// the engine calls on threads of its own.
std::atomic<bool> gEngineRunning{false};
// Starts that failed in a row; after kMaxStartFailures the generators
// take over.
int gStartFailures = 0;
constexpr int kMaxStartFailures = 3;
// When a start last failed (SDL's ms), so a broken engine is not asked
// again every frame.
std::uint64_t gLastFailureMs = 0;
constexpr std::uint64_t kRetryMs = 1000;
// What was logged once.
bool gLoggedKind[4] = {};
int gPlayErrorsLogged = 0;
constexpr int kMaxPlayErrorsLogged = 5;

UIImpactFeedbackGenerator* gFlipperGenerator = nil;
UIImpactFeedbackGenerator* gHitGenerator = nil;
UIImpactFeedbackGenerator* gDrainGenerator = nil;
UINotificationFeedbackGenerator* gJackpotGenerator = nil;

void useGenerators(const char* why) {
  gPath = HapticPath::Generators;
  gEngine = nil;
  gPatterns = nil;
  gEngineRunning.store(false);
  // Made without a view: the deprecation the SDK announces for this
  // initialiser has no date, and a generator made for a view plays only
  // through the view's interaction.
  gFlipperGenerator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleMedium];
  gHitGenerator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleRigid];
  gDrainGenerator = [[UIImpactFeedbackGenerator alloc] initWithStyle:UIImpactFeedbackStyleHeavy];
  gJackpotGenerator = [[UINotificationFeedbackGenerator alloc] init];
  [gFlipperGenerator prepare];
  [gHitGenerator prepare];
  [gDrainGenerator prepare];
  [gJackpotGenerator prepare];
  os_log(hapticsLog(), "Haptics path: UIKit feedback generators (%{public}s)", why);
}

void prepareGenerators() {
  if (gPath != HapticPath::Generators) {
    return;
  }
  [gFlipperGenerator prepare];
  [gHitGenerator prepare];
  [gDrainGenerator prepare];
  [gJackpotGenerator prepare];
}

NSArray<CHHapticPattern*>* makePatterns() {
  NSMutableArray<CHHapticPattern*>* patterns = [NSMutableArray arrayWithCapacity:4];
  for (const Haptic kind : {Haptic::Flipper, Haptic::Hit, Haptic::Drain, Haptic::Jackpot}) {
    NSMutableArray<CHHapticEvent*>* events = [NSMutableArray array];
    for (const HapticPulse& pulse : hapticPattern(kind)) {
      NSArray<CHHapticEventParameter*>* const parameters = @[
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticIntensity
                                                      value:pulse.intensity],
        [[CHHapticEventParameter alloc] initWithParameterID:CHHapticEventParameterIDHapticSharpness
                                                      value:pulse.sharpness],
      ];
      CHHapticEvent* const event =
          pulse.continuous()
              ? [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticContinuous
                                              parameters:parameters
                                            relativeTime:pulse.atSeconds
                                                duration:pulse.durationSeconds]
              : [[CHHapticEvent alloc] initWithEventType:CHHapticEventTypeHapticTransient
                                              parameters:parameters
                                            relativeTime:pulse.atSeconds];
      [events addObject:event];
    }
    NSError* error = nil;
    CHHapticPattern* const pattern = [[CHHapticPattern alloc] initWithEvents:events
                                                                  parameters:@[]
                                                                       error:&error];
    if (pattern == nil) {
      os_log_error(hapticsLog(), "Haptic pattern %{public}s refused: %{public}@", kindName(kind),
                   error.localizedDescription);
      return nil;
    }
    [patterns addObject:pattern];
  }
  return patterns;
}

// Starts the engine if it is not running. Main thread. False if it could
// not be started (then, after a few failures, the generators take over).
bool startEngine(const char* why) {
  if (gPath != HapticPath::Engine || gEngine == nil) {
    return false;
  }
  if (gEngineRunning.load()) {
    return true;
  }
  const std::uint64_t now = SDL_GetTicks();
  if (gStartFailures > 0 && now - gLastFailureMs < kRetryMs) {
    return false;
  }
  NSError* error = nil;
  if (![gEngine startAndReturnError:&error]) {
    ++gStartFailures;
    gLastFailureMs = now;
    os_log_error(hapticsLog(), "Haptic engine failed to start (%{public}s, try %d): %{public}@",
                 why, gStartFailures, error.localizedDescription);
    if (gStartFailures >= kMaxStartFailures) {
      useGenerators("the engine would not start");
    }
    return false;
  }
  gStartFailures = 0;
  gEngineRunning.store(true);
  os_log(hapticsLog(), "Haptic engine started (%{public}s)", why);
  return true;
}

void setUpHaptics() {
  if (gPath != HapticPath::None) {
    return;
  }
  if (UIDevice.currentDevice.userInterfaceIdiom != UIUserInterfaceIdiomPhone) {
    os_log(hapticsLog(), "Haptics path: none (not a phone)");
    gPath = HapticPath::Off;
    return;
  }
  if (!CHHapticEngine.capabilitiesForHardware.supportsHaptics) {
    useGenerators("Core Haptics unsupported on this hardware");
    return;
  }
  NSError* error = nil;
  CHHapticEngine* const engine = [[CHHapticEngine alloc] initAndReturnError:&error];
  NSArray<CHHapticPattern*>* const patterns = engine != nil ? makePatterns() : nil;
  if (engine == nil || patterns == nil) {
    os_log_error(hapticsLog(), "Haptic engine could not be made: %{public}@",
                 error.localizedDescription);
    useGenerators("no engine");
    return;
  }
  // Haptics alone: the engine never touches the audio session, which SDL
  // configures for the game's sound.
  engine.playsHapticsOnly = YES;
  engine.stoppedHandler = ^(CHHapticEngineStoppedReason reason) {
    gEngineRunning.store(false);
    os_log(hapticsLog(), "Haptic engine stopped, reason %ld", static_cast<long>(reason));
  };
  engine.resetHandler = ^{
    gEngineRunning.store(false);
    os_log(hapticsLog(), "Haptic engine reset by the system; restarting");
    dispatch_async(dispatch_get_main_queue(), ^{
      (void)startEngine("after reset");
    });
  };
  gEngine = engine;
  gPatterns = patterns;
  gPath = HapticPath::Engine;
  os_log(hapticsLog(), "Haptics path: Core Haptics engine");
  (void)startEngine("set up");
}

void observeLifecycle() {
  static dispatch_once_t once;
  dispatch_once(&once, ^{
    NSNotificationCenter* const centre = [NSNotificationCenter defaultCenter];
    NSOperationQueue* const main = [NSOperationQueue mainQueue];
    void (^const active)(NSNotification*) = ^(NSNotification*) {
      if (gPath == HapticPath::Engine) {
        (void)startEngine("became active");
      } else {
        prepareGenerators();
      }
    };
    [centre addObserverForName:UIApplicationDidBecomeActiveNotification
                        object:nil
                         queue:main
                    usingBlock:active];
    [centre addObserverForName:UISceneDidActivateNotification
                        object:nil
                         queue:main
                    usingBlock:active];
    [centre addObserverForName:UIWindowDidBecomeKeyNotification
                        object:nil
                         queue:main
                    usingBlock:^(NSNotification*) {
                      prepareGenerators();
                    }];
    [centre addObserverForName:UIApplicationDidEnterBackgroundNotification
                        object:nil
                         queue:main
                    usingBlock:^(NSNotification*) {
                      os_log(hapticsLog(), "In the background: the system stops the engine");
                    }];
  });
}

bool playOnEngine(Haptic kind) {
  const auto index = static_cast<NSUInteger>(std::to_underlying(kind));
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (!startEngine("play")) {
      return false;
    }
    NSError* error = nil;
    id<CHHapticPatternPlayer> const player = [gEngine createPlayerWithPattern:gPatterns[index]
                                                                        error:&error];
    if (player != nil && [player startAtTime:CHHapticTimeImmediate error:&error]) {
      return true;
    }
    if (gPlayErrorsLogged < kMaxPlayErrorsLogged) {
      ++gPlayErrorsLogged;
      os_log_error(hapticsLog(), "Haptic %{public}s not played: %{public}@", kindName(kind),
                   error.localizedDescription);
    }
    // Most likely stopped without the handler having run yet: once more
    // from a fresh start.
    gEngineRunning.store(false);
  }
  return false;
}

void playOnGenerators(Haptic kind) {
  switch (kind) {
    case Haptic::Flipper:
      [gFlipperGenerator impactOccurredWithIntensity:0.75];
      [gFlipperGenerator prepare];
      break;
    case Haptic::Hit:
      [gHitGenerator impactOccurredWithIntensity:1.0];
      [gHitGenerator prepare];
      break;
    case Haptic::Drain:
      [gDrainGenerator impactOccurredWithIntensity:1.0];
      [gDrainGenerator prepare];
      break;
    case Haptic::Jackpot:
      [gJackpotGenerator notificationOccurred:UINotificationFeedbackTypeSuccess];
      [gJackpotGenerator prepare];
      break;
  }
}

}  // namespace

void attachWindow(SDL_Window* window) noexcept {
  @autoreleasepool {
    observeLifecycle();
    setUpHaptics();
    if (window == nullptr) {
      return;
    }
    UIWindow* const uiWindow = (__bridge UIWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    gView = uiWindow.rootViewController.view;
  }
}

bool hapticsAvailable() noexcept {
  // Phones have the engine; iPads have none.
  return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone;
}

void haptic(Haptic kind) noexcept {
  if (![NSThread isMainThread]) {
    // Never so from the game's loop; kept safe all the same.
    dispatch_async(dispatch_get_main_queue(), ^{
      haptic(kind);
    });
    return;
  }
  @autoreleasepool {
    setUpHaptics();
    const auto index = static_cast<std::size_t>(std::to_underlying(kind));
    bool played = false;
    if (gPath == HapticPath::Engine) {
      played = playOnEngine(kind);
    }
    if (gPath == HapticPath::Generators && !played) {
      playOnGenerators(kind);
      played = gFlipperGenerator != nil;
    }
    if (played && !gLoggedKind[index]) {
      gLoggedKind[index] = true;
      os_log(hapticsLog(), "First %{public}s haptic played (%{public}s)", kindName(kind),
             gPath == HapticPath::Engine ? "Core Haptics" : "UIKit");
    }
  }
}

void requestReview() noexcept {
  @autoreleasepool {
    pinball_request_review();
  }
}

bool setAccessibilityElements(std::span<const AccessibleElement> elements, std::uint32_t eventType,
                              AccessibilityFocus focus) noexcept {
  // UIKit's, so on the main thread alone, where the game's loop runs. From
  // another, nothing is changed and the caller tries again. The labels are
  // copied into the elements: the caller's strings die with the call.
  if (![NSThread isMainThread]) {
    NSLog(@"Accessibility elements not replaced: not on the main thread");
    return false;
  }
  @autoreleasepool {
    UIView* const view = gView;
    if (view == nil) {
      return true;
    }
    if (elements.empty()) {
      view.accessibilityElements = nil;
      return true;
    }
    NSMutableArray* const list = [NSMutableArray arrayWithCapacity:elements.size()];
    for (const AccessibleElement& element : elements) {
      PinballAccessibleElement* const item =
          [[PinballAccessibleElement alloc] initWithAccessibilityContainer:view];
      item.accessibilityLabel = [[NSString alloc] initWithBytes:element.label.data()
                                                         length:element.label.size()
                                                       encoding:NSUTF8StringEncoding];
      item.accessibilityFrameInContainerSpace =
          CGRectMake(static_cast<CGFloat>(element.frame.x), static_cast<CGFloat>(element.frame.y),
                     static_cast<CGFloat>(element.frame.w), static_cast<CGFloat>(element.frame.h));
      const bool control = element.role == AccessRole::Button;
      UIAccessibilityTraits traits = UIAccessibilityTraitButton;
      if (element.role == AccessRole::Header) {
        traits = UIAccessibilityTraitHeader;
      } else if (element.role == AccessRole::Text) {
        traits = UIAccessibilityTraitStaticText;
      } else if (!element.enabled) {
        traits |= UIAccessibilityTraitNotEnabled;
      }
      if (element.selected) {
        traits |= UIAccessibilityTraitSelected;
      }
      item.accessibilityTraits = traits;
      item.eventType = control && element.enabled ? eventType : 0;
      item.code = element.code;
      [list addObject:item];
    }
    view.accessibilityElements = list;
    id const target = focus.element < list.count ? list[focus.element] : list.firstObject;
    switch (focus.kind) {
      case AccessibilityFocus::Kind::Stay:
        break;
      case AccessibilityFocus::Kind::Screen:
        UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification, target);
        break;
      case AccessibilityFocus::Kind::Page:
        UIAccessibilityPostNotification(UIAccessibilityLayoutChangedNotification, target);
        break;
    }
  }
  return true;
}

}  // namespace pinball::ios
