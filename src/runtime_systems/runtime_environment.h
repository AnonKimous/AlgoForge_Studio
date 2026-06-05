#pragma once

#include "common_data/common_data.h"

#include <functional>
#include <memory>

namespace runtime_systems {

class ImGuiVulkanRuntime;
class SdlWindow;
struct SdlWindowDeleter {
  void operator()(SdlWindow* window) const;
};
struct ImGuiVulkanRuntimeDeleter {
  void operator()(ImGuiVulkanRuntime* runtime) const;
};

class RuntimeEnvironment {
 public:
  RuntimeEnvironment();
  ~RuntimeEnvironment();

  using DrawCallback = std::function<void()>;

  bool Init(const char* window_title, int width, int height);
  bool Tick();
  void SetDrawCallback(DrawCallback callback);
  void Destroy();

  const InputState& input() const;
  Vec2 MousePosition() const;
  bool has_window() const { return static_cast<bool>(window_); }

 private:
  bool sdl_initialized_{false};
  std::unique_ptr<SdlWindow, SdlWindowDeleter> window_{};
  std::unique_ptr<ImGuiVulkanRuntime, ImGuiVulkanRuntimeDeleter> imgui_runtime_{};
};

}  // namespace runtime_systems
