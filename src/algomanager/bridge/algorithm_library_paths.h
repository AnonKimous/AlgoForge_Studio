#pragma once

#include <array>
#include <filesystem>
#include <initializer_list>
#include <string>

#ifndef ALGOFORGE_PROJECT_ROOT
#define ALGOFORGE_PROJECT_ROOT ""
#endif

namespace algorithm {
namespace library_paths {

namespace fs = std::filesystem;

enum class AlgorithmLibraryRuntimeBuildFlavor {
  Debug = 0,
  ReleaseWithDebugInfo = 1,
};

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

inline fs::path ResolveFirstExistingPathFromPaths(
  std::initializer_list<fs::path> candidates,
  PathType path_type) {
  std::error_code ec;
  for (const fs::path& candidate : candidates) {
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
  return {};
}

inline fs::path ResolveExecutableDirectory() {
  std::error_code ec;
  return fs::current_path(ec);
}

inline fs::path ResolveExecutableProjectRoot() {
  std::error_code ec;
  const fs::path configured_root(ALGOFORGE_PROJECT_ROOT);
  if (!configured_root.empty() &&
      fs::is_directory(configured_root / "algorithmLib/algorithmSrc", ec)) {
    return configured_root;
  }

  fs::path candidate = ResolveExecutableDirectory();
  for (size_t depth = 0u; depth < 6u && !candidate.empty(); ++depth) {
    ec.clear();
    if (fs::is_directory(candidate / "algorithmLib/algorithmSrc", ec)) {
      return candidate;
    }
    candidate = candidate.parent_path();
  }
  return {};
}

inline fs::path ResolveAlgorithmLibrarySourceRoot() {
  const fs::path executable_project_root = ResolveExecutableProjectRoot();
  if (!executable_project_root.empty()) {
    return executable_project_root / "algorithmLib/algorithmSrc";
  }
  return ResolveFirstExistingPath({
    "../../algorithmLib/algorithmSrc",
    "../../../algorithmLib/algorithmSrc",
    "../algorithmLib/algorithmSrc",
    "algorithmLib/algorithmSrc",
  }, PathType::Directory);
}

inline fs::path ResolveAlgorithmLibraryRuntimeRoot() {
  const fs::path executable_project_root = ResolveExecutableProjectRoot();
  if (!executable_project_root.empty()) {
    return executable_project_root / "algorithmLib/algorithmruntimeLib";
  }
  return ResolveFirstExistingPath({
    "../../algorithmLib/algorithmruntimeLib",
    "../../../algorithmLib/algorithmruntimeLib",
    "../algorithmLib/algorithmruntimeLib",
    "algorithmLib/algorithmruntimeLib",
  }, PathType::Directory);
}

inline AlgorithmLibraryRuntimeBuildFlavor& CurrentAlgorithmLibraryRuntimeBuildFlavorStorage() {
  static AlgorithmLibraryRuntimeBuildFlavor flavor = AlgorithmLibraryRuntimeBuildFlavor::Debug;
  return flavor;
}

inline void SetAlgorithmLibraryRuntimeBuildFlavor(AlgorithmLibraryRuntimeBuildFlavor flavor) {
  CurrentAlgorithmLibraryRuntimeBuildFlavorStorage() = flavor;
}

inline AlgorithmLibraryRuntimeBuildFlavor GetAlgorithmLibraryRuntimeBuildFlavor() {
  return CurrentAlgorithmLibraryRuntimeBuildFlavorStorage();
}

inline fs::path ResolveAlgorithmLibraryChildRoot(
  const fs::path& parent_root,
  const char* child_name) {
  if (parent_root.empty() || !child_name || !*child_name) {
    return {};
  }
  return parent_root / child_name;
}

inline fs::path ResolveAlgorithmLibrarySourceNormRoot() {
  return ResolveAlgorithmLibraryChildRoot(ResolveAlgorithmLibrarySourceRoot(), "norm");
}

inline fs::path ResolveAlgorithmLibrarySourcePipelineRoot() {
  return ResolveAlgorithmLibraryChildRoot(ResolveAlgorithmLibrarySourceRoot(), "pipeline");
}

inline fs::path ResolveAlgorithmLibraryRuntimeNormRoot() {
  return ResolveAlgorithmLibraryChildRoot(ResolveAlgorithmLibraryRuntimeRoot(), "norm");
}

inline fs::path ResolveAlgorithmLibraryRuntimePipelineRoot() {
  return ResolveAlgorithmLibraryChildRoot(ResolveAlgorithmLibraryRuntimeRoot(), "pipeline");
}

inline fs::path ResolveTestDataRoot() {
  const fs::path source_root = ResolveAlgorithmLibrarySourceRoot();
  if (source_root.empty()) {
    return {};
  }
  return source_root.parent_path().parent_path() / "testData";
}

inline fs::path ResolveAlgorithmLibrarySourceDebugInfoRoot(const fs::path& category_root) {
  return ResolveAlgorithmLibraryChildRoot(category_root, "debugInfo");
}

inline fs::path ResolveAlgorithmLibraryRuntimeDebugInfoRoot(const fs::path& category_root) {
  return ResolveAlgorithmLibraryChildRoot(category_root, "debugInfo");
}

inline fs::path ResolveAlgorithmLibrarySourceNormDebugInfoRoot() {
  return ResolveAlgorithmLibrarySourceDebugInfoRoot(ResolveAlgorithmLibrarySourceNormRoot());
}

inline fs::path ResolveAlgorithmLibrarySourcePipelineDebugInfoRoot() {
  return ResolveAlgorithmLibrarySourceDebugInfoRoot(ResolveAlgorithmLibrarySourcePipelineRoot());
}

inline fs::path ResolveAlgorithmLibraryRuntimeNormDebugInfoRoot() {
  return ResolveTestDataRoot() / "norm" / "debugInfo";
}

inline fs::path ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() {
  return ResolveTestDataRoot() / "pipeline" / "debugInfo";
}

inline fs::path ResolveProjectRootFromAlgorithmLibraryRoot(const fs::path& algorithm_library_root) {
  if (algorithm_library_root.empty()) {
    return {};
  }
  return algorithm_library_root.parent_path().parent_path();
}

inline fs::path ResolvePipelineRunnerArtifactRoot() {
  return ResolveTestDataRoot() / "pipeline_runner";
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
