#pragma once
// Call only from the GUI thread, with the editor peer's NSView handle.
bool apeIsNativeViewVisible(void* nativeView) noexcept;
