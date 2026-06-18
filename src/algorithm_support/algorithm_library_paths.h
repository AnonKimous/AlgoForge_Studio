#pragma once

#include <filesystem>
#include <initializer_list>
#include <string>

namespace algorithm {
namespace library_paths {

namespace fs = std::filesystem;

enum class PathType {
  File,
  Directory,
};

inline fs::path ResolveFirstExistingPath(
  std::initializer_list<const char*> candidates,
  PathType path_type) {
  std::error_code ec;
  for (const char* candidate_text : candidates) {
    const fs::path candidate(candidate_text);
    if (!fs::exists(candidate, ec)) {
      continue;
    }
    if (path_type == PathType::Directory) {
      if (fs::is_directory(candidate, ec)) {
        return candidate;
      }
    } else if (fs::is_regular_file(candidate, ec)) {
      return candidate;
    }
  }
  if (candidates.size() == 0u) {
    return {};
  }
  return fs::path(*candidates.begin());
}

inline fs::path ResolveAlgorithmLibrarySourceRoot() {
  return ResolveFirstExistingPath({
    "algorithmLib/algorithmSrc",
    "../algorithmLib/algorithmSrc",
    "../../algorithmLib/algorithmSrc",
    "../../../algorithmLib/algorithmSrc",
  }, PathType::Directory);
}

inline fs::path ResolveAlgorithmLibraryRuntimeRoot() {
  return ResolveFirstExistingPath({
    "algorithmLib/algorithmruntimeLib",
    "../algorithmLib/algorithmruntimeLib",
    "../../algorithmLib/algorithmruntimeLib",
    "../../../algorithmLib/algorithmruntimeLib",
  }, PathType::Directory);
}

inline fs::path ResolveProjectRootFromAlgorithmLibraryRoot(const fs::path& algorithm_library_root) {
  if (algorithm_library_root.empty()) {
    return {};
  }
  return algorithm_library_root.parent_path().parent_path();
}

inline fs::path ResolvePackageRelativePath(
  const fs::path& package_root,
  const fs::path& source_root) {
  if (package_root.empty() || source_root.empty()) {
    return {};
  }

  const fs::path relative_path = package_root.lexically_relative(source_root);
  const std::string relative_text = relative_path.generic_string();
  if (relative_path.empty() || relative_path == "." || relative_text.rfind("..", 0u) == 0u) {
    return {};
  }
  return relative_path;
}

inline fs::path ResolveRuntimePackageRoot(
  const fs::path& package_root,
  const fs::path& source_root,
  const fs::path& runtime_root) {
  if (package_root.empty() || source_root.empty() || runtime_root.empty()) {
    return {};
  }

  const fs::path relative_path = ResolvePackageRelativePath(package_root, source_root);
  if (relative_path.empty()) {
    return {};
  }
  return runtime_root / relative_path;
}

inline fs::path ResolveAlgorithmRelativePath(
  const fs::path& root,
  const fs::path& package_relative_root,
  const std::string& relative_path) {
  if (relative_path.empty()) {
    return {};
  }

  const fs::path path(relative_path);
  if (path.is_absolute()) {
    return path;
  }

  return root / package_relative_root / relative_path;
}

}  // namespace library_paths
}  // namespace algorithm
