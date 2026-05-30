#pragma once

namespace foundation {

struct InputState {
  int mouse_x{};
  int mouse_y{};
  bool left_down{};
  bool left_pressed{};
  bool left_released{};
  bool left_double_clicked{};
  bool ctrl_down{};
};

}  // namespace foundation

using foundation::InputState;
