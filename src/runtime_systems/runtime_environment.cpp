#define RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD 1
#include "runtime_environment.h"
#include "job_system.h"
#include "algorithm_management/algorithm_manager.h"

#include <SDL3/SDL.h>

#include "runtime_systems/render/imgui_vulkan_runtime.h"
#include "runtime_systems/window/sdl_window.h"

#include <cassert>
#include <string>
#include <utility>

namespace runtime_systems {

void SdlWindowDeleter::operator()(SdlWindow* window) const {
  delete window;
}

void ImGuiVulkanRuntimeDeleter::operator()(ImGuiVulkanRuntime* runtime) const {
  delete runtime;
}

RuntimeEnvironment::RuntimeEnvironment() = default;

RuntimeEnvironment::~RuntimeEnvironment() {
  Destroy();
}

bool RuntimeEnvironment::Init(
  const char* window_title,
  int width,
  int height,
  RuntimeExecutionSymbols execution_symbols) {
  if (!InitializeJobSystem()) {
    return false;
  }

  if (window_ || imgui_runtime_) {
    execution_symbols_ = execution_symbols;
    return true;
  }

  if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
    ShutdownJobSystem();
    return false;
  }
  sdl_initialized_ = true;

  try {
    window_ = std::unique_ptr<SdlWindow, SdlWindowDeleter>(
      new SdlWindow(window_title ? window_title : "debugTool", width, height));
    imgui_runtime_ = std::unique_ptr<ImGuiVulkanRuntime, ImGuiVulkanRuntimeDeleter>(
      new ImGuiVulkanRuntime());
    if (!imgui_runtime_->Init(window_->native_handle().window, window_title ? window_title : "debugTool")) {
      Destroy();
      return false;
    }
    execution_symbols_ = execution_symbols;
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

void RuntimeEnvironment::SetRenderPreviewRequest(RenderPreviewRequest request) {
  if (request.valid) {
    assert(!request.stage_name.empty() && "Render preview request is missing a stage name.");
    assert(!request.storage_buffers.empty() && "Render preview request is missing storage buffers.");
  }
  if (imgui_runtime_) {
    imgui_runtime_->SetRenderPreviewRequest(std::move(request));
  } else {
    assert(!request.valid && "A valid render preview request arrived before the runtime was initialized.");
  }
}

void RuntimeEnvironment::SetRenderPreviewExtent(ImVec2 extent) {
  if (imgui_runtime_) {
    imgui_runtime_->SetRenderPreviewExtent(extent);
  }
}

void RuntimeEnvironment::ClearGpuRuntimeCaches() {
  if (imgui_runtime_) {
    imgui_runtime_->ClearGpuRuntimeCaches();
  }
}

bool RuntimeEnvironment::HasRenderPreviewTexture() const {
  return imgui_runtime_ ? imgui_runtime_->HasRenderPreviewTexture() : false;
}

std::string RuntimeEnvironment::RenderPreviewDebugSummary() const {
  return imgui_runtime_ ? imgui_runtime_->RenderPreviewDebugSummary() : std::string("preview=uninitialized");
}

ImTextureID RuntimeEnvironment::RenderPreviewTextureId() const {
  return imgui_runtime_ ? imgui_runtime_->RenderPreviewTextureId() : ImTextureID{};
}

ImVec2 RuntimeEnvironment::RenderPreviewTextureSize() const {
  return imgui_runtime_ ? imgui_runtime_->RenderPreviewTextureSize() : ImVec2{};
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
  algorithm_management::AlgorithmScheduler::Instance().Clear();
  ShutdownJobSystem();
  execution_symbols_ = {};
  if (sdl_initialized_) {
    SDL_Quit();
    sdl_initialized_ = false;
  }
}

}  // namespace runtime_systems

#undef RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD
