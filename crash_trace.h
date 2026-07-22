#pragma once

// Install fatal-signal handlers that print a symbolized backtrace. A no-op
// unless built with IMWAY_FILL_GARBAGE (the imway_test binary), where it turns
// a use-after-free the poisoned allocator provokes into a readable stack in
// the compositor log instead of a bare kernel segfault line.
void installCrashTracer();
