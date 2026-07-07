#pragma once

#include <filesystem>
#include <string>

namespace algorithm {

struct AlgorithmPackageLocation {
  std::string algorithm_name;
  std::string manifest_name;
  std::filesystem::path manifest_path;
  std::filesystem::path package_root;
  std::filesystem::path runtime_package_root;
  std::filesystem::path source_manifest_path;
  std::filesystem::path source_package_root;
  std::filesystem::path package_file_path;
  std::filesystem::path plugin_module_path;
  bool from_algo_file{false};
  bool has_plugin_module{false};
  bool valid{false};

  void Clear() {
    algorithm_name.clear();
    manifest_name.clear();
    manifest_path.clear();
    package_root.clear();
    runtime_package_root.clear();
    source_manifest_path.clear();
    source_package_root.clear();
    package_file_path.clear();
    plugin_module_path.clear();
    from_algo_file = false;
    has_plugin_module = false;
    valid = false;
  }
};

bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message = nullptr);

// Compile-time helper for standalone algorithm plugins. Mainline runtime code
// should continue to go through algorithm_manager.h as the public manager exit.
bool TryResolveAlgorithmPackageLocationForPluginCompile(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message = nullptr);

}  // namespace algorithm
