#pragma once

#include "common_data/input_state.h"
#include "common_data/window_handle.h"

#include "common_data/vector_types.h"

#include <SDL3/SDL.h>

#include <string>

namespace runtime_systems {

class SdlWindow {
 public:
  SdlWindow(const char* title, int width, int height);
  ~SdlWindow();

  bool ProcessEvents();
  WindowHandle native_handle() const { return WindowHandle{window_}; }
  int width() const { return width_; }
  int height() const { return height_; }
  const InputState& input() const { return input_; }
  Vec2 MousePosition() const;
  void SetTitle(const std::string& title);

 private:
  SDL_Window* window_{};
  SDL_WindowID window_id_{0};
  int width_{};
  int height_{};
  InputState input_{};
  bool quit_requested_{false};
};

}  // namespace runtime_systems

using runtime_systems::SdlWindow;
