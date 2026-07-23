#pragma once

#include "weak_ptr.h"

struct Composer;
struct DialogState;
struct Toplevel;

// the Terminate/Wait escalation for an unresponsive window: opened by the
// close cross of a window in the ANR state. Terminate raises the toplevel's
// terminateRequested — wayland kills the client by pid; Wait just closes.
// The dialog dies with its target (or when the target recovers).
void drawAnrDialog(Composer& c, Weak<Toplevel>& target, bool toggle, DialogState** state);
