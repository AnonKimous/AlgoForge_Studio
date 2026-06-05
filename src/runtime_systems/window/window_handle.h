#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD)
#error "Do not include runtime_systems/window/window_handle.h directly. Use runtime_systems/runtime_environment.h."
#endif

struct SDL_Window;

namespace runtime_systems {

struct WindowHandle {
  SDL_Window* window{};
};

}  // namespace runtime_systems

using runtime_systems::WindowHandle;
