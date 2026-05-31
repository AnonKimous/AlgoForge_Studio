#pragma once

struct SDL_Window;

namespace common_data {

struct WindowHandle {
  SDL_Window* window{};
};

}  // namespace common_data

using common_data::WindowHandle;
