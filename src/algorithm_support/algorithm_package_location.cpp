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

std::string _SanitizePathKey(const fs::path& value) {
  std::string normalized;
  const std::string text = value.generic_string();
  normalized.reserve(text.size());
  for (unsigned char ch : text) {
    if (std::isalnum(ch) != 0) {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
    } else {
      normalized.push_back('_');
    }
  }
  if (normalized.empty()) {
    normalized = "algo";
  }
  return normalized;
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
  const std::string relative_key_prefix = _SanitizePathKey(relative_package_path);
  const std::string cache_dir_name =
    relative_key_prefix + "_" +
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

  for (fs::directory_iterator iter(cache_root, ec), end; !ec && iter != end; iter.increment(ec)) {
    if (ec || !iter->is_directory()) {
      continue;
    }
    const std::string sibling_name = iter->path().filename().string();
    const std::string expected_prefix = relative_key_prefix + "_" + std::to_string(relative_key_hash) + "_";
    if (iter->path() == mounted_root || iter->path() == mounted_root_tmp) {
      continue;
    }
    if (sibling_name.rfind(expected_prefix, 0u) == 0u) {
      fs::remove_all(iter->path(), ec);
      ec.clear();
    }
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

  const fs::path search_root = library_paths::ResolveAlgorithmLibrarySourceRoot();
  static std::mutex manifest_lookup_cache_mutex;
  static std::unordered_map<std::string, ManifestLookupCacheEntry> manifest_lookup_cache;
  const std::string cache_key = search_root.generic_string() + "|" + trimmed_name;
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

  std::error_code ec;
  if (!fs::exists(search_root, ec) || !fs::is_directory(search_root, ec)) {
    if (out_error_message) {
      *out_error_message = "Manifest search root does not exist: " + search_root.generic_string();
    }
    return ManifestLookupResult::Error;
  }

  const fs::path requested_path = fs::path(trimmed_name);
  if (_TryUseManifestPathCandidate(requested_path, out_manifest_path)) {
    return ManifestLookupResult::Found;
  }
  const fs::path requested_package_path = fs::path(trimmed_name + "_package.json");
  if (_TryUsePackagePathCandidate(requested_package_path, out_manifest_path)) {
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
  if (_TryUseManifestPathCandidate(search_root / requested_path, out_manifest_path)) {
    return ManifestLookupResult::Found;
  }
  if (_TryUsePackagePathCandidate(search_root / (trimmed_name + "_package.json"), out_manifest_path)) {
    return ManifestLookupResult::Found;
  }

  std::vector<fs::path> matches;
  for (fs::recursive_directory_iterator iter(search_root, ec), end; !ec && iter != end; iter.increment(ec)) {
    if (ec) break;
    if (!iter->is_regular_file()) continue;
    const fs::path candidate = iter->path();
    if (_ToLower(candidate.extension().string()) != ".json") continue;
    if (_ManifestNameMatches(candidate, trimmed_name)) {
      matches.push_back(candidate);
    }
  }

  if (matches.size() == 1u) {
    *out_manifest_path = matches.front();
    std::lock_guard<std::mutex> lock(manifest_lookup_cache_mutex);
    manifest_lookup_cache[cache_key] = ManifestLookupCacheEntry{
      .result = ManifestLookupResult::Found,
      .manifest_path = matches.front(),
      .error_message = {}
    };
    return ManifestLookupResult::Found;
  }

  if (out_error_message) {
    if (matches.empty()) {
      *out_error_message =
        "No manifest named '" + trimmed_name + "' was found under " + search_root.generic_string() + ".";
    } else {
      std::ostringstream message;
      message << "Manifest name '" << trimmed_name << "' is ambiguous under "
              << search_root.generic_string() << ": ";
      for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0u) {
          message << ", ";
        }
        message << matches[i].generic_string();
      }
      *out_error_message = message.str();
    }
  }
  const ManifestLookupResult result =
    matches.empty() ? ManifestLookupResult::NotFound : ManifestLookupResult::Error;
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

  const fs::path runtime_root = library_paths::ResolveAlgorithmLibraryRuntimeRoot();
  static std::mutex algo_file_lookup_cache_mutex;
  static std::unordered_map<std::string, AlgoFileLookupCacheEntry> algo_file_lookup_cache;
  const std::string cache_key = runtime_root.generic_string() + "|" + trimmed_name;
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

  std::error_code ec;
  if (!fs::exists(runtime_root, ec) || !fs::is_directory(runtime_root, ec)) {
    if (out_error_message) {
      *out_error_message = "Algorithm runtime root does not exist: " + runtime_root.generic_string();
    }
    return ManifestLookupResult::Error;
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

  const fs::path flat_candidate = runtime_root / (trimmed_name + ".algo");
  if (_TryUseAlgoFilePathCandidate(flat_candidate, out_package_file_path)) {
    std::lock_guard<std::mutex> lock(algo_file_lookup_cache_mutex);
    algo_file_lookup_cache[cache_key] = AlgoFileLookupCacheEntry{
      .result = ManifestLookupResult::Found,
      .package_file_path = *out_package_file_path,
      .error_message = {}
    };
    return ManifestLookupResult::Found;
  }

  std::vector<fs::path> matches;
  for (fs::recursive_directory_iterator iter(runtime_root, ec), end; !ec && iter != end; iter.increment(ec)) {
    if (ec) {
      break;
    }
    if (!iter->is_regular_file()) {
      continue;
    }
    const fs::path candidate = iter->path();
    if (_AlgoFileNameMatches(candidate, trimmed_name)) {
      matches.push_back(candidate);
    }
  }

  if (matches.size() == 1u) {
    *out_package_file_path = matches.front();
    std::lock_guard<std::mutex> lock(algo_file_lookup_cache_mutex);
    algo_file_lookup_cache[cache_key] = AlgoFileLookupCacheEntry{
      .result = ManifestLookupResult::Found,
      .package_file_path = matches.front(),
      .error_message = {}
    };
    return ManifestLookupResult::Found;
  }

  if (out_error_message) {
    if (matches.empty()) {
      *out_error_message =
        "No .algo package named '" + trimmed_name + "' was found under " + runtime_root.generic_string() + ".";
    } else {
      std::ostringstream message;
      message << ".algo package name '" << trimmed_name << "' is ambiguous under "
              << runtime_root.generic_string() << ": ";
      for (size_t i = 0; i < matches.size(); ++i) {
        if (i > 0u) {
          message << ", ";
        }
        message << matches[i].generic_string();
      }
      *out_error_message = message.str();
    }
  }

  const ManifestLookupResult result =
    matches.empty() ? ManifestLookupResult::NotFound : ManifestLookupResult::Error;
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

bool TryResolveAlgorithmPackageLocation(
  const std::string& algorithm_name,
  AlgorithmPackageLocation* out_location,
  std::string* out_error_message) {
  return _TryResolveAlgorithmPackageLocationFromAlgoFile(
    algorithm_name,
    out_location,
    out_error_message);
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
