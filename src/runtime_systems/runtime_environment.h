#pragma once

#include "common_data/input_state.h"
#include "common_data/vector_types.h"

#include <functional>
#include <memory>

namespace runtime_systems {

class ImGuiVulkanRuntime;
class SdlWindow;

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
  std::unique_ptr<SdlWindow> window_{};
  std::unique_ptr<ImGuiVulkanRuntime> imgui_runtime_{};
};

}  // namespace runtime_systems
