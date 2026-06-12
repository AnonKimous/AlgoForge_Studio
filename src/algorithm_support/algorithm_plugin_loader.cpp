#include "algorithm_support/algorithm_plugin_loader.h"

#include "algorithm_management/algorithm_manager.h"

#include <windows.h>

#include <filesystem>
#include <utility>

namespace algorithm_support {

namespace {

using CreateBundleFn = bool (*)(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm_library_plugin::AlgorithmPluginBundle* out_bundle);

using CreateRuntimeReflectorFn = bool (*)(
  const algorithm_library_plugin::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector);

void _SetErrorMessage(std::string* out_error_message, std::string message) {
  if (out_error_message) {
    *out_error_message = std::move(message);
  }
}

std::shared_ptr<void> _LoadModule(const std::filesystem::path& path) {
  const std::wstring wide_path = path.wstring();
  HMODULE module = LoadLibraryW(wide_path.c_str());
  if (!module) {
    return {};
  }

  return std::shared_ptr<void>(
    module,
    [](void* handle) {
      if (handle) {
        FreeLibrary(static_cast<HMODULE>(handle));
      }
    });
}

template <typename T>
std::shared_ptr<T> _WrapPluginObject(
  T* object,
  void (*destroy_fn)(T*),
  const std::shared_ptr<void>& module_guard) {
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

  const std::shared_ptr<void> module_guard = _LoadModule(plugin_path);
  if (!module_guard) {
    _SetErrorMessage(out_error_message, "Failed to load algorithm plugin module: " + plugin_path.string());
    return false;
  }

  const auto create_bundle_fn = reinterpret_cast<CreateBundleFn>(
    GetProcAddress(static_cast<HMODULE>(module_guard.get()), "AlgorithmPlugin_CreateBundle"));
  if (!create_bundle_fn) {
    _SetErrorMessage(out_error_message, "Algorithm plugin is missing AlgorithmPlugin_CreateBundle: " + plugin_path.string());
    return false;
  }

  algorithm_library_plugin::AlgorithmPluginRequest request{};
  const std::string algorithm_library_root = package_location.package_root.has_parent_path()
    ? package_location.package_root.parent_path().generic_string()
    : package_location.package_root.generic_string();
  const std::string algorithm_folder = package_location.package_root.filename().generic_string();
  request.algorithm_name = package_location.algorithm_name.c_str();
  request.algorithm_library_root = algorithm_library_root.c_str();
  request.algorithm_folder = algorithm_folder.c_str();

  algorithm_library_plugin::AlgorithmPluginBundle bundle{};
  if (!create_bundle_fn(&request, &bundle)) {
    _SetErrorMessage(out_error_message, "Algorithm plugin rejected bundle creation: " + plugin_path.string());
    return false;
  }

  if (request.api_version != algorithm_library_plugin::kAlgorithmPluginApiVersion) {
    _SetErrorMessage(
      out_error_message,
      "Algorithm plugin request ABI version mismatch for: " + plugin_path.string());
    return false;
  }
  if (bundle.api_version != algorithm_library_plugin::kAlgorithmPluginApiVersion) {
    _SetErrorMessage(
      out_error_message,
      "Algorithm plugin bundle ABI version mismatch for: " + plugin_path.string());
    return false;
  }

  out_components->cpu_symbol = bundle.cpu_symbol;
  out_components->gpu_symbol = bundle.gpu_symbol;

  if (bundle.reflector && bundle.destroy_reflector) {
    out_components->reflector = _WrapPluginObject(bundle.reflector, bundle.destroy_reflector, module_guard);
  }
  if (bundle.decomposer && bundle.destroy_decomposer) {
    out_components->decomposer = _WrapPluginObject(bundle.decomposer, bundle.destroy_decomposer, module_guard);
  }
  if (bundle.intervention && bundle.destroy_intervention) {
    out_components->intervention = _WrapPluginObject(
      bundle.intervention,
      bundle.destroy_intervention,
      module_guard);
  }
  if (bundle.temporary_test_executor && bundle.destroy_temporary_test_executor) {
    out_components->temporary_test_executor = _WrapPluginObject(
      bundle.temporary_test_executor,
      bundle.destroy_temporary_test_executor,
      module_guard);
  }

  const auto create_reflector_fn = reinterpret_cast<CreateRuntimeReflectorFn>(
    GetProcAddress(static_cast<HMODULE>(module_guard.get()), "AlgorithmPlugin_CreateRuntimeReflector"));
  if (create_reflector_fn) {
    algorithm::AlgorithmReflector runtime_reflector{};
    if (create_reflector_fn(&request, &runtime_reflector)) {
      out_components->runtime_reflector =
        std::make_shared<algorithm::AlgorithmReflector>(std::move(runtime_reflector));
    }
  }

  _SetErrorMessage(out_error_message, {});
  return true;
}

}  // namespace algorithm_support
