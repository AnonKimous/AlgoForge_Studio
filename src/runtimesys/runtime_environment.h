#pragma once

#if !defined(RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD) && !defined(RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE)
#error "Do not include runtimesys/runtime_environment.h directly. Use runtimesys/runtime_systems.h."
#endif

#include "common_data/common_data.h"
#include "runtimesys/debug_tool_recording.h"
#include "runtimesys/render/render_preview_request.h"

#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/gpu_job_system.h"
#include "runtimesys/job_system.h"
#include "runtimesys/runtime_gpu_context.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include <filesystem>
#include <functional>
#include <memory>
#include <imgui.h>

namespace runtimesys {

class ImGuiVulkanRuntime;
class SdlWindow;

using RuntimeShutdownCallback = void (*)();
using RuntimeVkCacheClearCallback = void (*)();

void SetRuntimeShutdownCallback(RuntimeShutdownCallback callback);
void SetRuntimeVkCacheClearCallback(RuntimeVkCacheClearCallback callback);
void InvokeRuntimeVkCacheClearCallback();
void SetRuntimeDebugInfoRoot(std::filesystem::path root);
const std::filesystem::path& RuntimeDebugInfoRoot();

// Advisory runtime backend symbols. They default to true and do not change
// the main-thread callback execution path yet.
struct RuntimeExecutionSymbols {
  bool jobs_symbol{true};
  bool vk_symbol{true};
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
  void SetDebugInfoRoot(std::filesystem::path root);
  void SetSwapchainReadbackEnabled(bool enabled);
  void BeginDebugToolRecording();
  void EndDebugToolRecording();
  std::vector<DebugToolRecordedFrame> TakeDebugToolRecording();
  void SetRenderPreviewRequest(RenderPreviewRequest request);
  void SetRenderPreviewExtent(ImVec2 extent);
  void ClearVkRuntimeCaches();
  bool HasRenderPreviewTexture() const;
  bool ReadbackRenderPreviewTexture(std::vector<std::byte>* out_rgba, ImVec2* out_size);
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
  bool swapchain_readback_enabled_{false};
  ImVec2 offscreen_surface_extent_{1024.0f, 1024.0f};
};

}  // namespace runtimesys

namespace runtime_systems = runtimesys;
