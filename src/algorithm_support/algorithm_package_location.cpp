#include "algorithm_support/algorithm_package_location.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace algorithm {

namespace {

namespace fs = std::filesystem;

#ifndef ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT
#define ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT "src/capabilities/algorithm_library"
#endif

enum class ManifestLookupResult {
  Found,
  NotFound,
  Error,
};

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

  const fs::path search_root = fs::path(ALGORITHM_MANAGEMENT_MANIFEST_SEARCH_ROOT);
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
  return matches.empty() ? ManifestLookupResult::NotFound : ManifestLookupResult::Error;
}

}  // namespace

bool TryResolveAlgorithmPackageLocation(
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

  const fs::path package_root = out_location->package_root;
  const fs::path plugin_candidates[] = {
    package_root / "Debug" / (trimmed_name + ".dll"),
    package_root / "Release" / (trimmed_name + ".dll"),
    package_root / (trimmed_name + ".dll"),
  };

  std::error_code ec;
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

}  // namespace algorithm
