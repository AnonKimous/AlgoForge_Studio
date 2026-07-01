#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_library_paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace algorithm {

namespace {

namespace fs = std::filesystem;

enum class ManifestLookupResult {
  Found,
  NotFound,
  Error,
};

struct ManifestLookupCacheEntry {
  ManifestLookupResult result{ManifestLookupResult::NotFound};
  fs::path manifest_path{};
  std::string error_message{};
};

struct AlgoFileLookupCacheEntry {
  ManifestLookupResult result{ManifestLookupResult::NotFound};
  fs::path package_file_path{};
  std::string error_message{};
};

constexpr char kAlgoFileMagic[8] = {'E', 'A', 'L', 'G', 'O', '0', '0', '1'};
constexpr uint32_t kAlgoFileVersion = 1u;
constexpr size_t kAlgoFileCopyBufferSize = 64u * 1024u;

std::string _Trim(std::string value) {
  const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), [&](char ch) {
    return !is_space(static_cast<unsigned char>(ch));
  }));
  value.erase(std::find_if(value.rbegin(), value.rend(), [&](char ch) {
    return !is_space(static_cast<unsigned char>(ch));
  }).base(), value.end());
  return value;
}

std::string _ToLower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool _ShouldEmitPackageLocationProbe(const std::string& algorithm_name) {
  return algorithm_name.find("v3a16_fireworks_pipeline_demo") != std::string::npos;
}

void _AppendPackageLocationProbe(const std::string& line) {
  const fs::path path = library_paths::ResolveAlgorithmLibraryRuntimePipelineDebugInfoRoot() /
    "package_location_probe.log";
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  std::ofstream file(path, std::ios::binary | std::ios::app);
  if (file) {
    file << line << '\n';
  }
}

std::string _NormalizeManifestLookupKey(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    }
  }
  return normalized;
}

std::string _GetNestedAlgorithmRootPrefix(const std::string& algorithm_name) {
  const std::string token = "_stage";
  const size_t stage_pos = algorithm_name.find(token);
  if (stage_pos == std::string::npos || stage_pos == 0u) {
    return {};
  }
  return algorithm_name.substr(0u, stage_pos);
}

uint64_t _HashStringFNV1a64(const std::string& value) {
  uint64_t hash = 1469598103934665603ull;
  for (unsigned char ch : value) {
    hash ^= static_cast<uint64_t>(ch);
    hash *= 1099511628211ull;
  }
  return hash;
}

bool _ReadExact(
  std::ifstream* stream,
  char* buffer,
  size_t size) {
  if (!stream || !buffer) {
    return false;
  }
  stream->read(buffer, static_cast<std::streamsize>(size));
  return stream->good() || stream->gcount() == static_cast<std::streamsize>(size);
}

bool _ReadUInt32(
  std::ifstream* stream,
  uint32_t* out_value) {
  if (!stream || !out_value) {
    return false;
  }
  return _ReadExact(stream, reinterpret_cast<char*>(out_value), sizeof(*out_value));
}

bool _ReadUInt64(
  std::ifstream* stream,
  uint64_t* out_value) {
  if (!stream || !out_value) {
    return false;
  }
  return _ReadExact(stream, reinterpret_cast<char*>(out_value), sizeof(*out_value));
}

bool _CopyExactBytesToFile(
  std::ifstream* source_stream,
  std::ofstream* destination_stream,
  uint64_t bytes_to_copy) {
  if (!source_stream || !destination_stream) {
    return false;
  }

  std::vector<char> buffer(kAlgoFileCopyBufferSize);
  uint64_t remaining = bytes_to_copy;
  while (remaining > 0u) {
    const size_t chunk_size = static_cast<size_t>(std::min<uint64_t>(remaining, buffer.size()));
    if (!_ReadExact(source_stream, buffer.data(), chunk_size)) {
      return false;
    }
    destination_stream->write(buffer.data(), static_cast<std::streamsize>(chunk_size));
    if (!destination_stream->good()) {
      return false;
    }
    remaining -= static_cast<uint64_t>(chunk_size);
  }
  return true;
}

bool _IsSafeAlgoFileRelativePath(const fs::path& path) {
  if (path.empty() || path.is_absolute()) {
    return false;
  }
  const fs::path normalized = path.lexically_normal();
  if (normalized.empty()) {
    return false;
  }
  for (const fs::path& component : normalized) {
    if (component == "..") {
      return false;
    }
  }
  return true;
}

bool _ExtractAlgoPackageFileToDirectory(
  const fs::path& package_file_path,
  const fs::path& destination_root,
  std::string* out_error_message) {
  std::ifstream stream(package_file_path, std::ios::binary);
  if (!stream) {
    if (out_error_message) {
      *out_error_message = "Failed to open .algo package file: " + package_file_path.generic_string();
    }
    return false;
  }

  char magic[sizeof(kAlgoFileMagic)]{};
  if (!_ReadExact(&stream, magic, sizeof(magic)) ||
      std::memcmp(magic, kAlgoFileMagic, sizeof(kAlgoFileMagic)) != 0) {
    if (out_error_message) {
      *out_error_message = "Invalid .algo package magic: " + package_file_path.generic_string();
    }
    return false;
  }

  uint32_t version = 0u;
  if (!_ReadUInt32(&stream, &version) || version != kAlgoFileVersion) {
    if (out_error_message) {
      *out_error_message = "Unsupported .algo package version in: " + package_file_path.generic_string();
    }
    return false;
  }

  uint32_t file_count = 0u;
  if (!_ReadUInt32(&stream, &file_count) || file_count == 0u) {
    if (out_error_message) {
      *out_error_message = ".algo package does not contain any files: " + package_file_path.generic_string();
    }
    return false;
  }

  std::error_code ec;
  for (uint32_t file_index = 0u; file_index < file_count; ++file_index) {
    uint32_t path_length = 0u;
    uint64_t file_size = 0u;
    if (!_ReadUInt32(&stream, &path_length) ||
        !_ReadUInt64(&stream, &file_size) ||
        path_length == 0u) {
      if (out_error_message) {
        *out_error_message = "Failed to read .algo package entry header: " + package_file_path.generic_string();
      }
      return false;
    }

    std::string relative_path_text(path_length, '\0');
    if (!_ReadExact(&stream, relative_path_text.data(), path_length)) {
      if (out_error_message) {
        *out_error_message = "Failed to read .algo package entry path: " + package_file_path.generic_string();
      }
      return false;
    }

    const fs::path relative_path = fs::path(relative_path_text).lexically_normal();
    if (!_IsSafeAlgoFileRelativePath(relative_path)) {
      if (out_error_message) {
        *out_error_message =
          ".algo package contains an unsafe entry path '" + relative_path.generic_string() +
          "': " + package_file_path.generic_string();
      }
      return false;
    }
    const fs::path destination_path = destination_root / relative_path;
    fs::create_directories(destination_path.parent_path(), ec);
    if (ec) {
      if (out_error_message) {
        *out_error_message =
          "Failed to create .algo extraction parent directory '" +
          destination_path.parent_path().generic_string() + "'.";
      }
      return false;
    }

    std::ofstream destination_stream(destination_path, std::ios::binary | std::ios::trunc);
    if (!destination_stream) {
      if (out_error_message) {
        *out_error_message = "Failed to create extracted .algo file: " + destination_path.generic_string();
      }
      return false;
    }
    if (!_CopyExactBytesToFile(&stream, &destination_stream, file_size)) {
      if (out_error_message) {
        *out_error_message =
          "Failed to extract .algo package entry '" + relative_path.generic_string() +
          "' to '" + destination_path.generic_string() + "'.";
      }
      return false;
    }
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _MountAlgoPackageFile(
  const fs::path& package_file_path,
  const fs::path& runtime_root,
  const fs::path& relative_package_path,
  fs::path* out_mounted_root,
  std::string* out_error_message) {
  if (!out_mounted_root) {
    if (out_error_message) {
      *out_error_message = "Mounted package root output pointer is null.";
    }
    return false;
  }

  std::error_code ec;
  const uintmax_t package_file_size = fs::file_size(package_file_path, ec);
  if (ec) {
    if (out_error_message) {
      *out_error_message = "Failed to query .algo package size: " + package_file_path.generic_string();
    }
    return false;
  }
  const auto package_file_write_time = fs::last_write_time(package_file_path, ec);
  if (ec) {
    if (out_error_message) {
      *out_error_message = "Failed to query .algo package timestamp: " + package_file_path.generic_string();
    }
    return false;
  }

  const std::string relative_key_text = relative_package_path.generic_string();
  const uint64_t relative_key_hash = _HashStringFNV1a64(relative_key_text);
  const std::string cache_dir_name =
    std::string("algo_") +
    std::to_string(relative_key_hash) + "_" +
    std::to_string(static_cast<unsigned long long>(package_file_size)) + "_" +
    std::to_string(static_cast<long long>(package_file_write_time.time_since_epoch().count()));

  const fs::path cache_root = runtime_root / ".algo_cache";
  const fs::path mounted_root = cache_root / cache_dir_name;
  const fs::path mounted_root_tmp = cache_root / (cache_dir_name + ".tmp");
  fs::create_directories(cache_root, ec);
  if (ec) {
    if (out_error_message) {
      *out_error_message = "Failed to create algorithm archive cache root: " + cache_root.generic_string();
    }
    return false;
  }

  if (fs::exists(mounted_root, ec) && fs::is_directory(mounted_root, ec)) {
    *out_mounted_root = mounted_root;
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  if (fs::exists(mounted_root_tmp, ec)) {
    fs::remove_all(mounted_root_tmp, ec);
    ec.clear();
  }
  fs::create_directories(mounted_root_tmp, ec);
  if (ec) {
    if (out_error_message) {
      *out_error_message =
        "Failed to create temporary .algo extraction root: " + mounted_root_tmp.generic_string();
    }
    return false;
  }

  std::string extract_error_message;
  if (!_ExtractAlgoPackageFileToDirectory(package_file_path, mounted_root_tmp, &extract_error_message)) {
    fs::remove_all(mounted_root_tmp, ec);
    if (out_error_message) {
      *out_error_message = extract_error_message;
    }
    return false;
  }

  fs::rename(mounted_root_tmp, mounted_root, ec);
  if (ec) {
    fs::remove_all(mounted_root_tmp, ec);
    if (out_error_message) {
      *out_error_message =
        "Failed to finalize extracted .algo cache directory: " + mounted_root.generic_string();
    }
    return false;
  }

  *out_mounted_root = mounted_root;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryUseManifestPathCandidate(const fs::path& candidate, fs::path* out_manifest_path) {
  if (!out_manifest_path) return false;

  std::error_code ec;
  if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
    *out_manifest_path = candidate;
    return true;
  }

  if (!candidate.has_extension()) {
    fs::path with_json_extension = candidate;
    with_json_extension += ".json";
    if (fs::exists(with_json_extension, ec) && fs::is_regular_file(with_json_extension, ec)) {
      *out_manifest_path = with_json_extension;
      return true;
    }
  }

  return false;
}

bool _TryUsePackagePathCandidate(const fs::path& candidate, fs::path* out_manifest_path) {
  if (!out_manifest_path) return false;

  std::error_code ec;
  if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
    *out_manifest_path = candidate;
    return true;
  }

  return false;
}

bool _ManifestNameMatches(const fs::path& candidate, const std::string& manifest_name) {
  const std::string wanted = _NormalizeManifestLookupKey(manifest_name);
  if (wanted.empty()) return false;

  const std::string filename = candidate.filename().string();
  const std::string stem = candidate.stem().string();
  const std::string normalized_filename = _NormalizeManifestLookupKey(filename);
  const std::string normalized_stem = _NormalizeManifestLookupKey(stem);
  return normalized_filename == wanted ||
    normalized_stem == wanted ||
    normalized_stem == _NormalizeManifestLookupKey(manifest_name + "_package");
}

ManifestLookupResult _ResolveManifestPathByName(
  const std::string& manifest_name,
  fs::path* out_manifest_path,
  std::string* out_error_message) {
  if (!out_manifest_path) return ManifestLookupResult::Error;

  const std::string trimmed_name = _Trim(manifest_name);
  if (trimmed_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Manifest name must not be empty.";
    }
    return ManifestLookupResult::Error;
  }

  const fs::path search_roots[] = {
    library_paths::ResolveAlgorithmLibrarySourceNormRoot(),
    library_paths::ResolveAlgorithmLibrarySourcePipelineRoot(),
  };
  static std::mutex manifest_lookup_cache_mutex;
  static std::unordered_map<std::string, ManifestLookupCacheEntry> manifest_lookup_cache;
  const std::string cache_key =
    search_roots[0].generic_string() + "|" +
    search_roots[1].generic_string() + "|" +
    trimmed_name;
  {
    std::lock_guard<std::mutex> lock(manifest_lookup_cache_mutex);
    const auto found = manifest_lookup_cache.find(cache_key);
    if (found != manifest_lookup_cache.end()) {
      if (found->second.result == ManifestLookupResult::Found) {
        *out_manifest_path = found->second.manifest_path;
      }
      if (out_error_message) {
        *out_error_message = found->second.error_message;
      }
      return found->second.result;
    }
  }

  for (const fs::path& search_root : search_roots) {
    if (search_root.empty()) {
      continue;
    }

    const fs::path requested_path = fs::path(trimmed_name);
    if (_TryUseManifestPathCandidate(search_root / requested_path, out_manifest_path)) {
      return ManifestLookupResult::Found;
    }
    const fs::path requested_package_path = fs::path(trimmed_name + "_package.json");
    if (_TryUsePackagePathCandidate(search_root / requested_package_path, out_manifest_path)) {
      return ManifestLookupResult::Found;
    }
    const fs::path folder_candidate = search_root / trimmed_name / (trimmed_name + ".json");
    if (_TryUseManifestPathCandidate(folder_candidate, out_manifest_path)) {
      return ManifestLookupResult::Found;
    }
    const fs::path package_folder_candidate = search_root / trimmed_name / (trimmed_name + "_package.json");
    if (_TryUsePackagePathCandidate(package_folder_candidate, out_manifest_path)) {
      return ManifestLookupResult::Found;
    }
    const std::string nested_root_prefix = _GetNestedAlgorithmRootPrefix(trimmed_name);
    if (!nested_root_prefix.empty()) {
      const fs::path nested_root_candidate = search_root / nested_root_prefix / trimmed_name;
      if (_TryUseManifestPathCandidate(nested_root_candidate / (trimmed_name + ".json"), out_manifest_path)) {
        return ManifestLookupResult::Found;
      }
      if (_TryUsePackagePathCandidate(
            nested_root_candidate / (trimmed_name + "_package.json"),
            out_manifest_path)) {
        return ManifestLookupResult::Found;
      }
    }
  }

  if (out_error_message) {
    *out_error_message =
      "No manifest named '" + trimmed_name + "' was found under " +
      library_paths::ResolveAlgorithmLibrarySourceRoot().generic_string() + ".";
  }
  const ManifestLookupResult result = ManifestLookupResult::NotFound;
  std::lock_guard<std::mutex> lock(manifest_lookup_cache_mutex);
  manifest_lookup_cache[cache_key] = ManifestLookupCacheEntry{
    .result = result,
    .manifest_path = {},
    .error_message = out_error_message ? *out_error_message : std::string{}
  };
  return result;
}

bool _TryUseAlgoFilePathCandidate(const fs::path& candidate, fs::path* out_package_file_path) {
  if (!out_package_file_path) {
    return false;
  }

  std::error_code ec;
  if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
    *out_package_file_path = candidate;
    return true;
  }
  return false;
}

bool _AlgoFileNameMatches(const fs::path& candidate, const std::string& algorithm_name) {
  const std::string wanted = _NormalizeManifestLookupKey(algorithm_name);
  if (wanted.empty()) {
    return false;
  }

  const std::string extension = _ToLower(candidate.extension().string());
  if (extension != ".algo") {
    return false;
  }

  const std::string stem = candidate.stem().string();
  return _NormalizeManifestLookupKey(stem) == wanted;
}

ManifestLookupResult _ResolveAlgoFilePathByName(
  const std::string& algorithm_name,
  fs::path* out_package_file_path,
  std::string* out_error_message) {
  if (!out_package_file_path) {
    if (out_error_message) {
      *out_error_message = ".algo package file output pointer is null.";
    }
    return ManifestLookupResult::Error;
  }

  const std::string trimmed_name = _Trim(algorithm_name);
  if (trimmed_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm name must not be empty.";
    }
    return ManifestLookupResult::Error;
  }

  const fs::path search_roots[] = {
    library_paths::ResolveAlgorithmLibraryRuntimeNormRoot(),
    library_paths::ResolveAlgorithmLibraryRuntimePipelineRoot(),
    library_paths::ResolveAlgorithmLibraryRuntimeRoot(),
  };
  static std::mutex algo_file_lookup_cache_mutex;
  static std::unordered_map<std::string, AlgoFileLookupCacheEntry> algo_file_lookup_cache;
  const std::string cache_key =
    search_roots[0].generic_string() + "|" +
    search_roots[1].generic_string() + "|" +
    trimmed_name;
  {
    std::lock_guard<std::mutex> lock(algo_file_lookup_cache_mutex);
    const auto found = algo_file_lookup_cache.find(cache_key);
    if (found != algo_file_lookup_cache.end()) {
      if (found->second.result == ManifestLookupResult::Found) {
        *out_package_file_path = found->second.package_file_path;
      }
      if (out_error_message) {
        *out_error_message = found->second.error_message;
      }
      return found->second.result;
    }
  }

  for (const fs::path& runtime_root : search_roots) {
    if (runtime_root.empty()) {
      continue;
    }

    const fs::path direct_candidate = runtime_root / trimmed_name / (trimmed_name + ".algo");
    if (_TryUseAlgoFilePathCandidate(direct_candidate, out_package_file_path)) {
      std::lock_guard<std::mutex> lock(algo_file_lookup_cache_mutex);
      algo_file_lookup_cache[cache_key] = AlgoFileLookupCacheEntry{
        .result = ManifestLookupResult::Found,
        .package_file_path = *out_package_file_path,
        .error_message = {}
      };
      return ManifestLookupResult::Found;
    }
    const std::string nested_root_prefix = _GetNestedAlgorithmRootPrefix(trimmed_name);
    if (!nested_root_prefix.empty()) {
      const fs::path nested_root_candidate = runtime_root / nested_root_prefix / trimmed_name;
      const fs::path nested_algo_candidate = nested_root_candidate / (trimmed_name + ".algo");
      if (_TryUseAlgoFilePathCandidate(nested_algo_candidate, out_package_file_path)) {
        std::lock_guard<std::mutex> lock(algo_file_lookup_cache_mutex);
        algo_file_lookup_cache[cache_key] = AlgoFileLookupCacheEntry{
          .result = ManifestLookupResult::Found,
          .package_file_path = *out_package_file_path,
          .error_message = {}
        };
        return ManifestLookupResult::Found;
      }
    }
  }

  if (out_error_message) {
    *out_error_message =
      "No .algo package named '" + trimmed_name + "' was found under " +
      library_paths::ResolveAlgorithmLibraryRuntimeRoot().generic_string() + ".";
  }

  const ManifestLookupResult result = ManifestLookupResult::NotFound;
  std::lock_guard<std::mutex> lock(algo_file_lookup_cache_mutex);
  algo_file_lookup_cache[cache_key] = AlgoFileLookupCacheEntry{
    .result = result,
    .package_file_path = {},
    .error_message = out_error_message ? *out_error_message : std::string{}
  };
  return result;
}

}  // namespace

bool _TryResolveAlgorithmPackageLocationFromAlgoFile(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message) {
  if (!out_location) {
    if (out_error_message) {
      *out_error_message = "AlgorithmPackageLocation output pointer is null.";
    }
    return false;
  }

  out_location->Clear();
  if (_ShouldEmitPackageLocationProbe(algorithm_name)) {
    _AppendPackageLocationProbe("resolve.begin name=" + algorithm_name);
  }

  fs::path package_file_path;
  const ManifestLookupResult lookup_result =
    _ResolveAlgoFilePathByName(algorithm_name, &package_file_path, out_error_message);
  if (lookup_result == ManifestLookupResult::NotFound) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return false;
  }
  if (lookup_result != ManifestLookupResult::Found) {
    return false;
  }
  if (_ShouldEmitPackageLocationProbe(algorithm_name)) {
    _AppendPackageLocationProbe("resolve.package_path.end path=" + package_file_path.generic_string());
  }

  const std::string trimmed_name = _Trim(algorithm_name);
  out_location->algorithm_name = trimmed_name;
  out_location->manifest_name = trimmed_name;
  const fs::path runtime_root = library_paths::ResolveAlgorithmLibraryRuntimeRoot();
  const fs::path relative_package_path =
    package_file_path.has_parent_path() && package_file_path.parent_path() != runtime_root
      ? package_file_path.parent_path().lexically_relative(runtime_root)
      : fs::path(trimmed_name);
  if (relative_package_path.empty() || relative_package_path.generic_string().rfind("..", 0u) == 0u) {
    if (out_error_message) {
      *out_error_message =
        "Failed to resolve .algo package relative path for '" + trimmed_name + "': " +
        package_file_path.generic_string();
    }
    return false;
  }

  fs::path mounted_package_root;
  std::string mount_error_message;
  if (_ShouldEmitPackageLocationProbe(algorithm_name)) {
    _AppendPackageLocationProbe("resolve.mount.begin path=" + package_file_path.generic_string());
  }
  if (!_MountAlgoPackageFile(
        package_file_path,
        runtime_root,
        relative_package_path,
        &mounted_package_root,
        &mount_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(mount_error_message);
    }
    return false;
  }
  if (_ShouldEmitPackageLocationProbe(algorithm_name)) {
    _AppendPackageLocationProbe("resolve.mount.end root=" + mounted_package_root.generic_string());
  }

  out_location->package_file_path = package_file_path;
  out_location->from_algo_file = true;
  out_location->manifest_path = mounted_package_root / (trimmed_name + "_package.json");
  out_location->package_root = mounted_package_root;
  out_location->runtime_package_root = mounted_package_root;

  std::error_code ec;
  if (!fs::exists(out_location->manifest_path, ec) || !fs::is_regular_file(out_location->manifest_path, ec)) {
    if (out_error_message) {
      *out_error_message =
        "Resolved algorithm manifest is unavailable for '" + trimmed_name + "': " +
        out_location->manifest_path.generic_string();
    }
    return false;
  }

  const fs::path plugin_candidates[] = {
    out_location->runtime_package_root / "Debug" / (trimmed_name + ".dll"),
    out_location->runtime_package_root / (trimmed_name + ".dll"),
  };

  for (const fs::path& candidate : plugin_candidates) {
    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
      out_location->plugin_module_path = candidate;
      out_location->has_plugin_module = true;
      break;
    }
  }

  out_location->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryResolveAlgorithmPackageLocationForPluginCompileLayout(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message) {
  if (!out_location) {
    if (out_error_message) {
      *out_error_message = "AlgorithmPackageLocation output pointer is null.";
    }
    return false;
  }

  out_location->Clear();

  fs::path manifest_path;
  const ManifestLookupResult lookup_result =
    _ResolveManifestPathByName(algorithm_name, &manifest_path, out_error_message);
  if (lookup_result == ManifestLookupResult::NotFound) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return false;
  }
  if (lookup_result != ManifestLookupResult::Found) {
    return false;
  }

  const std::string trimmed_name = _Trim(algorithm_name);
  out_location->algorithm_name = trimmed_name;
  out_location->manifest_name = trimmed_name;
  out_location->manifest_path = manifest_path;
  out_location->package_root = manifest_path.parent_path();
  out_location->source_manifest_path = manifest_path;
  out_location->source_package_root = manifest_path.parent_path();

  const fs::path source_root = library_paths::ResolveAlgorithmLibrarySourceRoot();
  const fs::path runtime_root = library_paths::ResolveAlgorithmLibraryRuntimeRoot();
  const fs::path loose_runtime_package_root = library_paths::ResolveRuntimePackageRoot(
    out_location->source_package_root,
    source_root,
    runtime_root);
  if (loose_runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Failed to resolve runtime package root for '" + trimmed_name + "' from source package path '" +
        out_location->source_package_root.generic_string() + "'.";
    }
    return false;
  }
  out_location->runtime_package_root = loose_runtime_package_root;

  std::error_code ec;
  const fs::path plugin_candidates[] = {
    out_location->runtime_package_root / "Debug" / (trimmed_name + ".dll"),
    out_location->runtime_package_root / (trimmed_name + ".dll"),
  };

  for (const fs::path& candidate : plugin_candidates) {
    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
      out_location->plugin_module_path = candidate;
      out_location->has_plugin_module = true;
      break;
    }
  }

  out_location->valid = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryResolveAlgorithmPackageLocationFromMountedCacheLayout(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message) {
  if (!out_location) {
    if (out_error_message) {
      *out_error_message = "AlgorithmPackageLocation output pointer is null.";
    }
    return false;
  }

  out_location->Clear();

  const std::string trimmed_name = _Trim(algorithm_name);
  if (trimmed_name.empty()) {
    if (out_error_message) {
      *out_error_message = "Algorithm name must not be empty.";
    }
    return false;
  }

  const fs::path runtime_root = library_paths::ResolveAlgorithmLibraryRuntimeRoot();
  const fs::path cache_root = runtime_root / ".algo_cache";
  std::error_code ec;
  if (!fs::exists(cache_root, ec) || !fs::is_directory(cache_root, ec)) {
    return false;
  }

  if (_ShouldEmitPackageLocationProbe(trimmed_name)) {
    _AppendPackageLocationProbe("resolve.cache.begin name=" + trimmed_name);
  }

  fs::path best_manifest_path;
  fs::path best_package_root;
  fs::file_time_type best_write_time{};
  bool found = false;
  const std::string package_file_name = trimmed_name + "_package.json";
  for (const fs::directory_entry& cache_entry : fs::directory_iterator(cache_root, ec)) {
    if (ec) {
      break;
    }
    if (!cache_entry.is_directory(ec)) {
      continue;
    }
    const fs::path candidate = cache_entry.path() / package_file_name;
    if (!fs::exists(candidate, ec) || !fs::is_regular_file(candidate, ec)) {
      continue;
    }

    const fs::file_time_type candidate_write_time = fs::last_write_time(candidate, ec);
    if (!found || candidate_write_time > best_write_time) {
      found = true;
      best_write_time = candidate_write_time;
      best_manifest_path = candidate;
      best_package_root = cache_entry.path();
    }
  }

  if (!found) {
    return false;
  }

  out_location->algorithm_name = trimmed_name;
  out_location->manifest_name = trimmed_name;
  out_location->manifest_path = best_manifest_path;
  out_location->package_root = best_package_root;
  out_location->runtime_package_root = best_package_root;

  fs::path source_manifest{};
  if (_ResolveManifestPathByName(trimmed_name, &source_manifest, nullptr) == ManifestLookupResult::Found) {
    out_location->source_manifest_path = source_manifest;
    out_location->source_package_root = source_manifest.parent_path();
  }

  const fs::path plugin_candidates[] = {
    out_location->runtime_package_root / "Debug" / (trimmed_name + ".dll"),
    out_location->runtime_package_root / (trimmed_name + ".dll"),
  };

  for (const fs::path& candidate : plugin_candidates) {
    if (fs::exists(candidate, ec) && fs::is_regular_file(candidate, ec)) {
      out_location->plugin_module_path = candidate;
      out_location->has_plugin_module = true;
      break;
    }
  }

  out_location->valid = true;
  if (_ShouldEmitPackageLocationProbe(trimmed_name)) {
    _AppendPackageLocationProbe("resolve.cache.end root=" + best_package_root.generic_string());
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message) {
  if (_TryResolveAlgorithmPackageLocationFromAlgoFile(
        algorithm_name,
        out_location,
        out_error_message)) {
    return true;
  }

  std::string mounted_cache_error_message;
  if (_TryResolveAlgorithmPackageLocationFromMountedCacheLayout(
        algorithm_name,
        out_location,
        &mounted_cache_error_message)) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::string plugin_compile_error_message;
  if (_TryResolveAlgorithmPackageLocationForPluginCompileLayout(
        algorithm_name,
        out_location,
        &plugin_compile_error_message)) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  if (out_error_message) {
    *out_error_message = !mounted_cache_error_message.empty()
      ? std::move(mounted_cache_error_message)
      : std::move(plugin_compile_error_message);
  }
  return false;
}

bool TryResolveAlgorithmPackageLocationForPluginCompile(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message) {
  return _TryResolveAlgorithmPackageLocationForPluginCompileLayout(
    algorithm_name,
    out_location,
    out_error_message);
}

}  // namespace algorithm
