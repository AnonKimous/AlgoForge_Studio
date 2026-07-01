#pragma once

#include "runtime_systems/runtime_systems.h"

#include <string>
#include <utility>

namespace debug_tool_backend::runtime_systems_hooker {

using RuntimeEnvironment = runtime_systems::RuntimeEnvironment;
using RenderPreviewRequest = runtime_systems::RenderPreviewRequest;
using RenderPreviewBuffer = runtime_systems::RenderPreviewBuffer;

class RuntimeSystemsHooker {
 public:
  bool Init(const char* window_title, int width, int height) {
    return runtime_environment_.Init(window_title, width, height);
  }

  bool Tick() {
    return runtime_environment_.Tick();
  }

  void Destroy() {
    runtime_environment_.Destroy();
  }

  bool has_window() const {
    return runtime_environment_.has_window();
  }

  const common_data::InputState& input() const {
    return runtime_environment_.input();
  }

  common_data::Vec2 MousePosition() const {
    return runtime_environment_.MousePosition();
  }

  void ClearGpuRuntimeCaches() {
    runtime_environment_.ClearGpuRuntimeCaches();
  }

  bool HasRenderPreviewTexture() const {
    return runtime_environment_.HasRenderPreviewTexture();
  }

  std::string RenderPreviewDebugSummary() const {
    return runtime_environment_.RenderPreviewDebugSummary();
  }

  ImTextureID RenderPreviewTextureId() const {
    return runtime_environment_.RenderPreviewTextureId();
  }

  ImVec2 RenderPreviewTextureSize() const {
    return runtime_environment_.RenderPreviewTextureSize();
  }

  void SetRenderPreviewExtent(ImVec2 extent) {
    runtime_environment_.SetRenderPreviewExtent(extent);
  }

  void SetRenderPreviewRequest(RenderPreviewRequest request) {
    render_preview_request_ = std::move(request);
    runtime_environment_.SetRenderPreviewRequest(render_preview_request_);
  }

  RuntimeEnvironment& runtime_environment() {
    return runtime_environment_;
  }

  const RuntimeEnvironment& runtime_environment() const {
    return runtime_environment_;
  }

 private:
  RuntimeEnvironment runtime_environment_{};
  RenderPreviewRequest render_preview_request_{};
};

}  // namespace debug_tool_backend::runtime_systems_hooker
