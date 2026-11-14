// Compiled for iOS only (CMakeLists.txt), as Objective-C++ with ARC. The
// Objective-C of the game on this system: three questions and a count,
// haptics, the review request (whose API is Swift's: ios/Review.swift) and
// what VoiceOver finds on the menu screens.
#include "platform/ios/IosSystem.hpp"

#import <AVFAudio/AVFAudio.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <SDL3/SDL.h>

#include <atomic>

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

// The game's view (SDL's), and the feedback generators made for it.
__weak UIView* gView = nil;
UIImpactFeedbackGenerator* gLight = nil;
UIImpactFeedbackGenerator* gHeavy = nil;
UINotificationFeedbackGenerator* gNotice = nil;

}  // namespace

void attachWindow(SDL_Window* window) noexcept {
  @autoreleasepool {
    if (window == nullptr) {
      return;
    }
    UIWindow* const uiWindow = (__bridge UIWindow*)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_UIKIT_WINDOW_POINTER, nullptr);
    UIView* const view = uiWindow.rootViewController.view;
    if (view == nil) {
      return;
    }
    gView = view;
    gLight = [UIImpactFeedbackGenerator feedbackGeneratorWithStyle:UIImpactFeedbackStyleLight
                                                           forView:view];
    gHeavy = [UIImpactFeedbackGenerator feedbackGeneratorWithStyle:UIImpactFeedbackStyleHeavy
                                                           forView:view];
    gNotice = [UINotificationFeedbackGenerator feedbackGeneratorForView:view];
    // Ready for the first, without its start-up delay.
    [gLight prepare];
    [gHeavy prepare];
    [gNotice prepare];
  }
}

bool hapticsAvailable() noexcept {
  // Phones have the engine; iPads have none.
  return UIDevice.currentDevice.userInterfaceIdiom == UIUserInterfaceIdiomPhone;
}

void haptic(Haptic kind) noexcept {
  @autoreleasepool {
    switch (kind) {
      case Haptic::Flipper:
        [gLight impactOccurredWithIntensity:0.45];
        break;
      case Haptic::Hit:
        [gLight impactOccurredWithIntensity:0.8];
        break;
      case Haptic::Drain:
        [gHeavy impactOccurred];
        break;
      case Haptic::Jackpot:
        [gNotice notificationOccurred:UINotificationFeedbackTypeSuccess];
        [gNotice prepare];
        break;
    }
    // The Taptic Engine stays ready for the next while the game is played.
    [gLight prepare];
  }
}

void requestReview() noexcept {
  @autoreleasepool {
    pinball_request_review();
  }
}

void setAccessibilityElements(std::span<const AccessibleElement> elements, std::uint32_t eventType,
                              bool screenChanged) noexcept {
  // UIKit's, so on the main thread alone, where the game's loop runs. The
  // labels are copied into the elements: the caller's strings die with the
  // call.
  if (![NSThread isMainThread]) {
    return;
  }
  @autoreleasepool {
    UIView* const view = gView;
    if (view == nil) {
      return;
    }
    if (elements.empty()) {
      view.accessibilityElements = nil;
      return;
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
      item.accessibilityTraits = traits;
      item.eventType = control && element.enabled ? eventType : 0;
      item.code = element.code;
      [list addObject:item];
    }
    view.accessibilityElements = list;
    if (screenChanged) {
      UIAccessibilityPostNotification(UIAccessibilityScreenChangedNotification, list.firstObject);
    }
  }
}

}  // namespace pinball::ios
