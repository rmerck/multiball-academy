// Compiled for iOS only (CMakeLists.txt), as Objective-C++ with ARC. The
// only Objective-C in the game on this system: three questions and a count.
#include "platform/ios/IosSystem.hpp"

#import <AVFAudio/AVFAudio.h>
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

#include <atomic>

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

// Device haptics are optional and not written. iPads have none; a phone's
// would need a feedback generator kept here.
void tap(float /*strength*/) noexcept {}

}  // namespace pinball::ios
