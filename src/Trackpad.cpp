#include "Trackpad.h"
#include <atomic>

namespace jmui {

static std::atomic<int> fingers { 0 };

int trackpadFingers() { return fingers.load(std::memory_order_relaxed); }
void setTrackpadFingers(int n) { fingers.store(n, std::memory_order_relaxed); }

#if ! defined(__APPLE__)
void hearTrackpad(void*) {} // nothing reports a touchpad's fingers here
#endif

} // namespace jmui
