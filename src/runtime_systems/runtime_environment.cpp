#define RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD 1
#include "runtime_environment.h"
#include "job_system.h"

#include <SDL3/SDL.h>

#include "runtime_systems/render/imgui_vulkan_runtime.h"
#include "runtime_systems/window/sdl_window.h"
#include "algorithm_catalog/algorithm_library_paths.h"

#include <cassert>
#include <iostream>
#include <string>
#include <utility>
#include <fstream>
#include <filesystem>

namespace runtime_systems {

namespace {

RuntimeShutdownCallback g_runtime_shutdown_callback = nullptr;
RuntimeVkCacheClearCallback g_runtime_vk_cache_clear_callback = nullptr;

void AppendRuntimeInitProbe(const std::string& line) {
  const std::filesystem::path probe_path =
    algorithm::library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() / "runtime_init_probe.log";
  std::error_code ec;
  std::filesystem::create_directories(probe_path.parent_path(), ec);
  std::ofstream file(probe_path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

}  // namespace

void SetRuntimeShutdownCallback(RuntimeShutdownCallback callback) {
  g_runtime_shutdown_callback = callback;
}

void SetRuntimeVkCacheClearCallback(RuntimeVkCacheClearCallback callback) {
  g_runtime_vk_cache_clear_callback = callback;
}

void InvokeRuntimeVkCacheClearCallback() {
  if (g_runtime_vk_cache_clear_callback) {
    g_runtime_vk_cache_clear_callback();
  }
}

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
  AppendRuntimeInitProbe("runtime_environment.init.begin");
  if (!InitializeJobSystem()) {
    AppendRuntimeInitProbe("runtime_environment.init.job_system_failed");
    return false;
  }

  if (window_ || imgui_runtime_) {
    execution_symbols_ = execution_symbols;
    AppendRuntimeInitProbe("runtime_environment.init.reused");
    return true;
  }

  SDL_SetHint("SDL_VIDEODRIVER", "windows");
  AppendRuntimeInitProbe("runtime_environment.init.sdl_hint_video_set");
  AppendRuntimeInitProbe("runtime_environment.init.sdl_video_begin");
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    AppendRuntimeInitProbe("runtime_environment.init.sdl_video_failed");
    ShutdownJobSystem();
    return false;
  }
  AppendRuntimeInitProbe("runtime_environment.init.sdl_video_end");
  AppendRuntimeInitProbe("runtime_environment.init.sdl_gamepad_begin");
  if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
    AppendRuntimeInitProbe("runtime_environment.init.sdl_gamepad_failed");
    SDL_Quit();
    ShutdownJobSystem();
    return false;
  }
  AppendRuntimeInitProbe("runtime_environment.init.sdl_gamepad_end");
  sdl_initialized_ = true;
  AppendRuntimeInitProbe("runtime_environment.init.sdl_end");

  try {
    AppendRuntimeInitProbe("runtime_environment.init.window_begin");
    window_ = std::unique_ptr<SdlWindow, SdlWindowDeleter>(
      new SdlWindow(window_title ? window_title : "debugTool", width, height));
    AppendRuntimeInitProbe("runtime_environment.init.window_end");
    AppendRuntimeInitProbe("runtime_environment.init.imgui_begin");
    imgui_runtime_ = std::unique_ptr<ImGuiVulkanRuntime, ImGuiVulkanRuntimeDeleter>(
      new ImGuiVulkanRuntime());
    if (!imgui_runtime_->Init(window_->native_handle().window, window_title ? window_title : "debugTool")) {
      AppendRuntimeInitProbe("runtime_environment.init.imgui_failed");
      Destroy();
      return false;
    }
    AppendRuntimeInitProbe("runtime_environment.init.imgui_end");
    execution_symbols_ = execution_symbols;
    AppendRuntimeInitProbe("runtime_environment.init.end");
    return true;
  } catch (...) {
    AppendRuntimeInitProbe("runtime_environment.init.throw");
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
    std::cerr << "Render preview request is missing a stage name.\n";
    assert(!request.stage_name.empty() && "Render preview request is missing a stage name.");
    std::cerr << "Render preview request is missing storage buffers.\n";
    assert(!request.storage_buffers.empty() && "Render preview request is missing storage buffers.");
  }
  if (imgui_runtime_) {
    imgui_runtime_->SetRenderPreviewRequest(std::move(request));
  } else {
    std::cerr << "A valid render preview request arrived before the runtime was initialized.\n";
    assert(!request.valid && "A valid render preview request arrived before the runtime was initialized.");
  }
}

void RuntimeEnvironment::SetRenderPreviewExtent(ImVec2 extent) {
  if (imgui_runtime_) {
    imgui_runtime_->SetRenderPreviewExtent(extent);
  }
}

void RuntimeEnvironment::ClearVkRuntimeCaches() {
  if (imgui_runtime_) {
    imgui_runtime_->ClearVkRuntimeCaches();
  }
}

bool RuntimeEnvironment::HasRenderPreviewTexture() const {
  return imgui_runtime_ ? imgui_runtime_->HasRenderPreviewTexture() : false;
}

bool RuntimeEnvironment::ReadbackRenderPreviewTexture(
  std::vector<std::byte>* out_rgba,
  ImVec2* out_size) {
  return imgui_runtime_
    ? imgui_runtime_->ReadbackRenderPreviewTexture(out_rgba, out_size)
    : false;
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
  if (g_runtime_shutdown_callback) {
    g_runtime_shutdown_callback();
  }
  ShutdownJobSystem();
  execution_symbols_ = {};
  if (sdl_initialized_) {
    SDL_Quit();
    sdl_initialized_ = false;
  }
}

}  // namespace runtime_systems

#undef RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD
