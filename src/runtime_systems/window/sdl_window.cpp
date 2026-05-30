#include "sdl_window.h"

#include <imgui_impl_sdl3.h>

#include <stdexcept>

namespace runtime_systems {

namespace {

std::string SdlError(const char* prefix) {
  return std::string(prefix) + " failed: " + SDL_GetError();
}

}  // namespace

SdlWindow::SdlWindow(const char* title, int width, int height)
    : width_(width), height_(height) {
  window_ = SDL_CreateWindow(title, width_, height_, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_HIDDEN);
  if (!window_) {
    throw std::runtime_error(SdlError("SDL_CreateWindow"));
  }
  window_id_ = SDL_GetWindowID(window_);
  SDL_ShowWindow(window_);
  SDL_GetWindowSizeInPixels(window_, &width_, &height_);
}

SdlWindow::~SdlWindow() {
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
}

bool SdlWindow::ProcessEvents() {
  input_.left_pressed = false;
  input_.left_released = false;
  input_.left_double_clicked = false;

  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    switch (event.type) {
      case SDL_EVENT_QUIT:
        quit_requested_ = true;
        break;
      case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        if (event.window.windowID == window_id_) {
          quit_requested_ = true;
        }
        break;
      case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        if (event.window.windowID == window_id_) {
          width_ = event.window.data1;
          height_ = event.window.data2;
        }
        break;
      case SDL_EVENT_MOUSE_MOTION:
        if (event.motion.windowID == window_id_) {
          input_.mouse_x = static_cast<int>(event.motion.x);
          input_.mouse_y = static_cast<int>(event.motion.y);
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.windowID == window_id_ && event.button.button == SDL_BUTTON_LEFT) {
          input_.left_down = true;
          input_.left_pressed = true;
          input_.left_double_clicked = event.button.clicks > 1;
        }
        break;
      case SDL_EVENT_MOUSE_BUTTON_UP:
        if (event.button.windowID == window_id_ && event.button.button == SDL_BUTTON_LEFT) {
          input_.left_down = false;
          input_.left_released = true;
        }
        break;
      default:
        break;
    }
  }

  if (window_) {
    SDL_GetWindowSizeInPixels(window_, &width_, &height_);
    float mouse_x = 0.0f;
    float mouse_y = 0.0f;
    SDL_GetMouseState(&mouse_x, &mouse_y);
    input_.mouse_x = static_cast<int>(mouse_x);
    input_.mouse_y = static_cast<int>(mouse_y);
  }

  input_.ctrl_down = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;

  return !quit_requested_;
}

Vec2 SdlWindow::MousePosition() const {
  return Vec2{static_cast<float>(input_.mouse_x), static_cast<float>(input_.mouse_y)};
}

void SdlWindow::SetTitle(const std::string& title) {
  if (window_) {
    SDL_SetWindowTitle(window_, title.c_str());
  }
}

}  // namespace runtime_systems
