#include "platform/MacMenu.hpp"

#import <Cocoa/Cocoa.h>

// Receives the menu action and hands it to the game as an SDL event, so the
// game reacts on its own loop like any other input.
@interface PinballMenuTarget : NSObject
@property(nonatomic) Uint32 eventType;
- (void)showAbout:(id)sender;
@end

@implementation PinballMenuTarget
- (void)showAbout:(id)sender {
  (void)sender;
  SDL_Event event;
  SDL_zero(event);
  event.type = self.eventType;
  SDL_PushEvent(&event);
}
@end

namespace pinball {

bool routeAboutMenu(Uint32 eventType, const char* title) {
  if (eventType == 0 || title == nullptr || NSApp == nil) {
    return false;
  }
  NSMenu* const mainMenu = [NSApp mainMenu];
  if (mainMenu == nil || [mainMenu numberOfItems] == 0) {
    return false;
  }
  NSMenu* const appMenu = [[mainMenu itemAtIndex:0] submenu];
  NSString* const name = [NSString stringWithUTF8String:title];
  if (appMenu == nil || name == nil) {
    return false;
  }

  // A menu item does not keep its target alive, so this does. One per process.
  static PinballMenuTarget* target = nil;
  NSString* const processName = [[NSProcessInfo processInfo] processName];
  bool routed = false;
  for (NSMenuItem* item in [appMenu itemArray]) {
    if ([processName length] > 0) {
      [item setTitle:[[item title] stringByReplacingOccurrencesOfString:processName
                                                             withString:name]];
    }
    if ([item action] == @selector(orderFrontStandardAboutPanel:)) {
      if (target == nil) {
        target = [[PinballMenuTarget alloc] init];
      }
      target.eventType = eventType;
      [item setTarget:target];
      [item setAction:@selector(showAbout:)];
      routed = true;
    }
  }
  return routed;
}

}  // namespace pinball
