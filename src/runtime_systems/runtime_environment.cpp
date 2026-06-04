#include "runtime_environment.h"

#include <SDL3/SDL.h>

#include "runtime_systems/render/imgui_vulkan_runtime.h"
#include "runtime_systems/window/sdl_window.h"

#include <string>
#include <utility>

namespace runtime_systems {

RuntimeEnvironment::RuntimeEnvironment() = default;

RuntimeEnvironment::~RuntimeEnvironment() {
  Destroy();
}

bool RuntimeEnvironment::Init(const char* window_title, int width, int height) {
  if (window_ || imgui_runtime_) {
    return true;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    return false;
  }
  sdl_initialized_ = true;

  try {
    window_ = std::make_unique<SdlWindow>(window_title ? window_title : "Interact & UI", width, height);
    imgui_runtime_ = std::make_unique<ImGuiVulkanRuntime>();
    if (!imgui_runtime_->Init(window_->native_handle().window, window_title ? window_title : "Interact & UI")) {
      Destroy();
      return false;
    }
    return true;
  } catch (...) {
    Destroy();
    throw;
  }
}

bool RuntimeEnvironment::Tick() {
  if (!window_ || !imgui_runtime_) {
    return false;
  }
  if (!window_->ProcessEvents()) {
    return false;
  }
  return imgui_runtime_->Tick(window_->native_handle().window);
}

void RuntimeEnvironment::SetDrawCallback(DrawCallback callback) {
  if (imgui_runtime_) {
    imgui_runtime_->SetDrawCallback(std::move(callback));
  }
}

const InputState& RuntimeEnvironment::input() const {
  static const InputState kEmptyInput{};
  return window_ ? window_->input() : kEmptyInput;
}

Vec2 RuntimeEnvironment::MousePosition() const {
  return window_ ? window_->MousePosition() : Vec2{};
}

void RuntimeEnvironment::Destroy() {
  if (imgui_runtime_) {
    imgui_runtime_->Destroy();
    imgui_runtime_.reset();
  }
  window_.reset();
  if (sdl_initialized_) {
    SDL_Quit();
    sdl_initialized_ = false;
  }
}

}  // namespace runtime_systems
