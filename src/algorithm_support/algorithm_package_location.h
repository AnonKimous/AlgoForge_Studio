#pragma once

#include <filesystem>
#include <string>

namespace algorithm {

struct AlgorithmPackageLocation {
  std::string algorithm_name;
  std::string manifest_name;
  std::filesystem::path manifest_path;
  std::filesystem::path package_root;
  std::filesystem::path plugin_module_path;
  bool has_plugin_module{false};
  bool valid{false};

  void Clear() {
    algorithm_name.clear();
    manifest_name.clear();
    manifest_path.clear();
    package_root.clear();
    plugin_module_path.clear();
    has_plugin_module = false;
    valid = false;
  }
};

bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message = nullptr);

}  // namespace algorithm
