#pragma once

#include <std/str/view.h>

// the `imway screenshot <path>` subcommand, a separate entry point next to
// the compositor's main(): a small wayland+vulkan imgui client that loads
// the raw ARGB image at <path> (a self-describing memfd the compositor
// handed over), lets the user crop it, and saves the result as a PNG under
// the pictures dir. returns the process exit code
int mainScreenshot(stl::StringView path);
