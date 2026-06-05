#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD)
#error "Do not include runtime_systems/window/sdl_window.h directly. Use runtime_systems/runtime_environment.h."
#endif

#include "common_data/common_data.h"
#include "runtime_systems/window/window_handle.h"

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
