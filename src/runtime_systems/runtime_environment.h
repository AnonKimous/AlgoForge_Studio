#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtime_systems/runtime_environment.h directly. Use runtime_systems/runtime_systems.h."
#endif

#include "common_data/common_data.h"
#include "runtime_systems/render/render_preview_request.h"

#include <functional>
#include <memory>
#include <imgui.h>

namespace runtime_systems {

class ImGuiVulkanRuntime;
class SdlWindow;

// Advisory runtime backend symbols. They default to true and do not change
// the main-thread callback execution path yet.
struct RuntimeExecutionSymbols {
  bool cpu_symbol{true};
  bool gpu_symbol{true};
};

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

  bool Init(
    const char* window_title,
    int width,
    int height,
    RuntimeExecutionSymbols execution_symbols = {});
  bool Tick();
  void SetDrawCallback(DrawCallback callback);
  void SetRenderPreviewRequest(RenderPreviewRequest request);
  void SetRenderPreviewExtent(ImVec2 extent);
  void ClearGpuRuntimeCaches();
  bool HasRenderPreviewTexture() const;
  std::string RenderPreviewDebugSummary() const;
  ImTextureID RenderPreviewTextureId() const;
  ImVec2 RenderPreviewTextureSize() const;
  void Destroy();

  const InputState& input() const;
  Vec2 MousePosition() const;
  bool has_window() const { return static_cast<bool>(window_); }

 private:
  bool sdl_initialized_{false};
  std::unique_ptr<SdlWindow, SdlWindowDeleter> window_{};
  std::unique_ptr<ImGuiVulkanRuntime, ImGuiVulkanRuntimeDeleter> imgui_runtime_{};
  RuntimeExecutionSymbols execution_symbols_{};
};

}  // namespace runtime_systems
