#pragma once

#include <filesystem>
#include <string>

namespace algorithm {
namespace package_paths {

namespace fs = std::filesystem;

inline fs::path ResolvePackageRoot(
  const fs::path& package_root,
  const fs::path& manifest_path) {
  if (!package_root.empty()) {
    return package_root;
  }
  return manifest_path.parent_path();
}

inline fs::path ResolvePackageJsonPath(
  const fs::path& package_root,
  const fs::path& manifest_path,
  const std::string& algorithm_name) {
  if (algorithm_name.empty()) {
    return {};
  }

  const fs::path resolved_root = ResolvePackageRoot(package_root, manifest_path);
  if (resolved_root.empty()) {
    return {};
  }

  return resolved_root / (algorithm_name + "_package.json");
}

inline fs::path ResolvePackageDefaultJsonPath(
  const fs::path& package_root,
  const fs::path& manifest_path) {
  const fs::path resolved_root = ResolvePackageRoot(package_root, manifest_path);
  if (resolved_root.empty()) {
    return {};
  }

  return resolved_root / "default.json";
}

}  // namespace package_paths
}  // namespace algorithm
