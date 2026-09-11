#pragma once

// Platform-neutral UI input state used by the boot page and settings page.
//
// Frontends map their native input to these logical buttons:
//   Nintendo Switch : hid A/B/X/Y, Minus/Plus, d-pad
//   macOS           : keyboard A/B/X/Y, Delete -> Minus, Enter -> Plus, arrows
namespace GbBoot
{
struct UiInputState
{
  // Held state (for auto-repeat navigation).
  bool up = false, down = false, left = false, right = false;

  // Edge (pressed this frame) state.
  bool a = false, b = false, x = false, y = false;
  bool minus = false, plus = false;
  bool upPressed = false, downPressed = false, leftPressed = false, rightPressed = false;
};
}
