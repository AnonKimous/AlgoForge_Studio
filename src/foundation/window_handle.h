#pragma once

struct SDL_Window;

namespace foundation {

struct WindowHandle {
  SDL_Window* window{};
};

}  // namespace foundation

using foundation::WindowHandle;
