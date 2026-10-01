#pragma once

#include <windows.h>

namespace gui {

// Show the control window and run it until the user closes it, or until the game
// exits when closeWithGame is set. Returns the process exit code.
int Run(HINSTANCE instance);

} // namespace gui
