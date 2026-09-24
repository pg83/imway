#pragma once

// Writes the coverage counters now, in a coverage-instrumented build, and
// does nothing in any other. A process that leaves by _exit, exec or a
// fatal signal never reaches the runtime's own at-exit hook; each such
// exit calls this first, so what the process ran is measured like any
// other's.
void flushCoverage();
