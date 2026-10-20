#include "Trackpad.h"
#import <AppKit/AppKit.h>
#include <objc/runtime.h>
#include <initializer_list>

namespace jmui {

// Every touch event: count the fingers touching.
static void touchesChanged(id, SEL, NSEvent* event)
{
    setTrackpadFingers((int) [[event touchesMatchingPhase: NSTouchPhaseTouching inView: nil] count]);
}

void hearTrackpad(void* nativeView)
{
    auto* view = (NSView*) nativeView;
    if (view == nil)
        return;
    view.allowedTouchTypes = NSTouchTypeMaskIndirect;
    // Fingers held still count too: macOS may take the ones holding a
    // click for fingers at rest, and leave them out otherwise.
    view.wantsRestingTouches = YES;
    // JUCE's view class has no touch handlers of its own, so ours go in.
    // class_addMethod leaves one that's already there alone, so this only
    // happens once.
    const Class viewClass = [view class];
    for (SEL selector : { @selector (touchesBeganWithEvent:), @selector (touchesMovedWithEvent:),
                          @selector (touchesEndedWithEvent:), @selector (touchesCancelledWithEvent:) })
        class_addMethod (viewClass, selector, (IMP) touchesChanged, "v@:@");
}

} // namespace jmui
