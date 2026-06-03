#pragma once

struct SDL_Window;

namespace runtime_systems {

struct WindowHandle {
  SDL_Window* window{};
};

}  // namespace runtime_systems

using runtime_systems::WindowHandle;
