#include "NativeVisibility.h"
#import <AppKit/AppKit.h>

bool apeIsNativeViewVisible(void* nativeView) noexcept {
    if (nativeView == nullptr) return false;
    NSView* view = (NSView*) nativeView;
    NSWindow* window = [view window];
    return window != nil && [window isVisible] && ![window isMiniaturized]
        && ![view isHiddenOrHasHiddenAncestor]
        && ([window occlusionState] & NSWindowOcclusionStateVisible) != 0;
}
