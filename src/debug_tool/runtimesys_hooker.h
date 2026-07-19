#pragma once

#include "runtimesys/runtime_systems.h"

#include <string>
#include <filesystem>
#include <utility>

namespace debug_tool_backend::runtimesys_hooker {

class RuntimesysHooker {
 public:
  bool Init(const char* window_title, int width, int height) {
    runtime_environment_.SetSwapchainReadbackEnabled(true);
    return runtime_environment_.Init(window_title, width, height);
  }

  void SetDebugInfoRoot(std::filesystem::path root) {
    runtime_environment_.SetDebugInfoRoot(std::move(root));
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

  void ClearVkRuntimeCaches() {
    runtime_environment_.ClearVkRuntimeCaches();
  }

  void BeginDebugToolRecording() {
    runtime_environment_.BeginDebugToolRecording();
  }

  void EndDebugToolRecording() {
    runtime_environment_.EndDebugToolRecording();
  }

  std::vector<runtimesys::DebugToolRecordedFrame> TakeDebugToolRecording() {
    return runtime_environment_.TakeDebugToolRecording();
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

  void SetRenderPreviewRequest(runtimesys::RenderPreviewRequest request) {
    render_preview_request_ = std::move(request);
    runtime_environment_.SetRenderPreviewRequest(render_preview_request_);
  }

  runtimesys::RuntimeEnvironment& runtime_environment() {
    return runtime_environment_;
  }

  const runtimesys::RuntimeEnvironment& runtime_environment() const {
    return runtime_environment_;
  }

 private:
  runtimesys::RuntimeEnvironment runtime_environment_{};
  runtimesys::RenderPreviewRequest render_preview_request_{};
};

}  // namespace debug_tool_backend::runtimesys_hooker
