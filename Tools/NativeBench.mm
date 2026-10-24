#import <AppKit/AppKit.h>
#include <iostream>
void prepareNativeBench() {
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
}
void showNativeBench(void* handle) {
    NSWindow* window = [(NSView*)handle window];
    [window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorFullScreenAuxiliary];
    [window setLevel:NSFloatingWindowLevel];
    [window makeKeyAndOrderFront:nil];
    [window orderFrontRegardless];
    [NSApp activateIgnoringOtherApps:YES];
}
void logNativeBench(void* handle) {
    NSView* view = (NSView*)handle;
    NSWindow* window = [view window];
    std::cout << "native_window_visible=" << bool([window isVisible])
              << " native_view_hidden=" << bool([view isHiddenOrHasHiddenAncestor])
              << " native_window_occluded=" << (([window occlusionState] & NSWindowOcclusionStateVisible) == 0) << std::endl;
}
