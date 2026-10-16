// gow2 - ReXGlue Recompiled Project
//
// Native mouse look. Feeds captured mouse motion into the engine's own PC
// mouse axes (MouseX/MouseY) instead of the emulated right stick, and keeps
// the game's aim friction/adhesion off the mouse. The pad path is untouched.
#pragma once

namespace rex::ui {
class Window;
}

namespace rex::glue {

bool NativeMouseAimWanted();
bool NativeMouseAimInstalled();
void DiscardNativeMouseInput();
void PrepareNativeMouseAim();
void InstallNativeMouseAim(rex::ui::Window* window);

}  // namespace rex::glue
