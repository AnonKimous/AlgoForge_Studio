#pragma once

#include <string>
#include <vector>

namespace common_data {

struct DroppedFile {
  std::string path;
  int x{};
  int y{};
};

struct InputState {
  int mouse_x{};
  int mouse_y{};
  bool left_down{};
  bool left_pressed{};
  bool left_released{};
  bool left_double_clicked{};
  bool ctrl_down{};
  std::vector<DroppedFile> dropped_files;
};

}  // namespace common_data

using common_data::InputState;
