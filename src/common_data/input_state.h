#pragma once

namespace common_data {

struct InputState {
  int mouse_x{};
  int mouse_y{};
  bool left_down{};
  bool left_pressed{};
  bool left_released{};
  bool left_double_clicked{};
  bool ctrl_down{};
};

}  // namespace common_data

using common_data::InputState;
