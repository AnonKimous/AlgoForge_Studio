#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/catalog/algorithm_vk_exec_support_detail.h"
#include "algomanager/catalog/algorithm_intervention_support_detail.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/catalog/algorithm_package_paths.h"
#include "algomanager/bridge/algorithm_types.h"
#include "algomanager/catalog/algorithm_json_utils.h"
#include "algomanager/catalog/algorithm_package_loader_detail.h"
#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/runtime_environment.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include "cJSON.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_loadso.h>

namespace algomanager { namespace algocatalog {

namespace fs = std::filesystem;

namespace {

using CreateBundleFn = bool (*)(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algomanager::algocatalog::AlgorithmPluginBundle* out_bundle);

using CreateRuntimeReflectorFn = bool (*)(
  const algomanager::algocatalog::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);
void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

using SharedObjectGuard = std::shared_ptr<SDL_SharedObject>;

SharedObjectGuard _LoadModule(
  const std::filesystem::path& path,
  std::string* out_error_message) {
  const fs::path absolute_path = fs::absolute(path).lexically_normal();
  SDL_SharedObject* module = SDL_LoadObject(absolute_path.string().c_str());
  if (!module) {
    if (out_error_message) {
      *out_error_message =
        "SDL_LoadObject failed for " + absolute_path.string() +
        ": " + SDL_GetError();
    }
    return {};
  }

  return SharedObjectGuard(module, [](SDL_SharedObject* handle) {
    if (handle) {
      SDL_UnloadObject(handle);
    }
  });
}

template <typename T>
std::shared_ptr<T> _WrapPluginObject(
  T* object,
  void (*destroy_fn)(T*),
  const SharedObjectGuard& module_guard) {
  if (!object || !destroy_fn) {
    return {};
  }
  return std::shared_ptr<T>(
    object,
    [module_guard, destroy_fn](T* ptr) {
      (void)module_guard;
      if (ptr) {
        destroy_fn(ptr);
      }
    });
}

}  // namespace

bool TryLoadAlgorithmPluginComponents(
  const algorithm::AlgorithmPackageLocation& package_location,
  AlgorithmPluginComponents* out_components,
  std::string* out_error_message) {
  if (!out_components) {
    _SetErrorMessage(out_error_message, "AlgorithmPluginComponents output pointer is null.");
    return false;
  }

  *out_components = {};
  if (!package_location.valid) {
    _SetErrorMessage(out_error_message, "Algorithm package location is invalid.");
    return false;
  }

  const std::filesystem::path plugin_path = package_location.plugin_module_path;
  if (plugin_path.empty()) {
    return false;
  }

  std::string load_error_message{};
  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    loader_detail::AppendPipelineRunnerProbe(
      "cache_loader_probe.log",
      "create_from_location.plugin_load.module.begin path=" +
        std::filesystem::absolute(plugin_path).lexically_normal().string());
  }
  const SharedObjectGuard module_guard = _LoadModule(plugin_path, &load_error_message);
  if (!module_guard) {
    if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
          ? package_location.manifest_name
          : package_location.algorithm_name)) {
      loader_detail::AppendPipelineRunnerProbe(
        "cache_loader_probe.log",
        "create_from_location.plugin_load.module.end failed error=" + load_error_message);
    }
    _SetErrorMessage(
      out_error_message,
      load_error_message.empty()
        ? "Failed to load algorithm plugin module: " +
          std::filesystem::absolute(plugin_path).lexically_normal().string()
        : load_error_message);
    return false;
  }
  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.module.end success");
  }

  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.proc.begin");
  }
  const auto create_bundle_fn = reinterpret_cast<CreateBundleFn>(
    SDL_LoadFunction(module_guard.get(), "AlgorithmPlugin_CreateBundle"));
  if (!create_bundle_fn) {
    if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
          ? package_location.manifest_name
          : package_location.algorithm_name)) {
      loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.proc.end missing");
    }
    _SetErrorMessage(out_error_message, "Algorithm plugin is missing AlgorithmPlugin_CreateBundle: " + plugin_path.string());
    return false;
  }
  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_load.proc.end success");
  }

  algomanager::algocatalog::AlgorithmPluginRequest request{};
  const std::filesystem::path plugin_request_package_root =
    package_location.source_package_root.empty()
      ? package_location.package_root
      : package_location.source_package_root;
  const std::string algorithm_library_root = plugin_request_package_root.has_parent_path()
    ? plugin_request_package_root.parent_path().generic_string()
    : plugin_request_package_root.generic_string();
  const std::string algorithm_folder = plugin_request_package_root.filename().generic_string();
  request.algorithm_name = package_location.algorithm_name.c_str();
  request.algorithm_library_root = algorithm_library_root.c_str();
  request.algorithm_folder = algorithm_folder.c_str();

  algomanager::algocatalog::AlgorithmPluginBundle bundle{};
  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_bundle.begin");
  }
  if (!create_bundle_fn(&request, &bundle)) {
    if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
          ? package_location.manifest_name
          : package_location.algorithm_name)) {
      loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_bundle.end failed");
    }
    _SetErrorMessage(out_error_message, "Algorithm plugin rejected bundle creation: " + plugin_path.string());
    return false;
  }
  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name)) {
    loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.plugin_bundle.end success");
  }

  if (request.api_version != algomanager::algocatalog::kAlgorithmPluginApiVersion) {
    _SetErrorMessage(
      out_error_message,
      "Algorithm plugin request ABI version mismatch for: " + plugin_path.string());
    return false;
  }
  if (bundle.api_version != algomanager::algocatalog::kAlgorithmPluginApiVersion) {
    _SetErrorMessage(
      out_error_message,
      "Algorithm plugin bundle ABI version mismatch for: " + plugin_path.string());
    return false;
  }

  out_components->jobs_symbol = bundle.jobs_symbol;
  out_components->vk_symbol = bundle.vk_symbol;
  out_components->cuda_symbol = bundle.cuda_symbol;
  out_components->compatibility_symbol = bundle.compatibility_symbol;
  out_components->reflector = bundle.reflector;
  out_components->intervention = bundle.intervention;

  if (bundle.vk_executor && bundle.destroy_vk_executor) {
    out_components->vk_executor = _WrapPluginObject(
      bundle.vk_executor,
      bundle.destroy_vk_executor,
      module_guard);
  }
  if (bundle.cuda_executor && bundle.destroy_cuda_executor) {
    out_components->cuda_executor = _WrapPluginObject(
      bundle.cuda_executor,
      bundle.destroy_cuda_executor,
      module_guard);
  }
  if (bundle.compatibility_executor && bundle.destroy_compatibility_executor) {
    out_components->compatibility_executor = _WrapPluginObject(
      bundle.compatibility_executor,
      bundle.destroy_compatibility_executor,
      module_guard);
  }
  if (bundle.jobs_executor && bundle.destroy_jobs_executor) {
    out_components->jobs_executor = _WrapPluginObject(
      bundle.jobs_executor,
      bundle.destroy_jobs_executor,
      module_guard);
  }

  if (loader_detail::ShouldEmitPipelineRunnerProbe(package_location.algorithm_name.empty()
        ? package_location.manifest_name
        : package_location.algorithm_name) &&
      bundle.reflector) {
    loader_detail::AppendPipelineRunnerProbe("cache_loader_probe.log", "create_from_location.reflector.skipped");
  }
  _SetErrorMessage(out_error_message, {});
  return true;
}


}  // namespace algocatalog
}  // namespace algomanager
