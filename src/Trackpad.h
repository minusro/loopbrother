#pragma once

// The fingers on a Mac trackpad. A trackpad click keeps the type it began
// as (one finger a left click, two a right one) for as long as it's held,
// so to switch between moving and sizing mid-drag the pad watches the
// fingers themselves. macOS only: elsewhere nothing reports them and the
// count stays 0.
namespace jmui {

// Makes a native view (an NSView, from ComponentPeer::getNativeHandle) hear
// trackpad touches. Safe to call again for the same view.
void hearTrackpad(void* nativeView);

// How many fingers are on the trackpad, as last heard; 0 when unknown.
int trackpadFingers();
void setTrackpadFingers(int fingers); // from the touch events, or a test

} // namespace jmui
