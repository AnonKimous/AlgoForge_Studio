#include "algorithm_support/algorithm_json_utils.h"
#include "algorithm_support/algorithm_intervention_support_detail.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_package_paths.h"
#include "algorithm_support/algorithm_protocol.h"

#include "cJSON.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm_support {

namespace {

namespace fs = std::filesystem;

bool LoadPackageRuntimeReflector(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  const fs::path package_json_path = algorithm::package_paths::ResolvePackageJsonPath(
    package_location.package_root,
    package_location.manifest_path,
    algorithm_name);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for reflector.";
    }
    return false;
  }

  const std::string json_text = json_utils::ReadAlgorithmPackageJsonFile(package_json_path);
  if (json_text.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to read package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    if (out_error_message) {
      *out_error_message = "Failed to parse package JSON file: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* reflector = cJSON_GetObjectItemCaseSensitive(root, "reflector");
  if (!reflector || !cJSON_IsObject(reflector)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package JSON is missing reflector section: " + package_json_path.generic_string();
    }
    return false;
  }

  const cJSON* items = cJSON_GetObjectItemCaseSensitive(reflector, "items");
  if (!items || !cJSON_IsArray(items)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package reflector is missing items: " + package_json_path.generic_string();
    }
    return false;
  }

  out_reflector->Clear();
  out_reflector->algorithm_name = package_location.algorithm_name;

  const cJSON* refresh_mode_item = cJSON_GetObjectItemCaseSensitive(reflector, "refreshMode");
  if (refresh_mode_item) {
    if (cJSON_IsString(refresh_mode_item) && refresh_mode_item->valuestring) {
      const std::string refresh_mode = refresh_mode_item->valuestring;
      if (refresh_mode == "captureOnce" ||
          refresh_mode == "capture_once" ||
          refresh_mode == "onceAfterCompletion" ||
          refresh_mode == "once_after_completion") {
        out_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion;
      } else if (refresh_mode == "everyTick" ||
                 refresh_mode == "every_tick") {
        out_reflector->refresh_mode = algorithm::AlgorithmReflectionRefreshMode::EveryTick;
      } else {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package reflector refreshMode is invalid: " + package_json_path.generic_string();
        }
        return false;
      }
    } else if (cJSON_IsBool(refresh_mode_item)) {
      out_reflector->refresh_mode = cJSON_IsTrue(refresh_mode_item)
        ? algorithm::AlgorithmReflectionRefreshMode::CaptureOnceAfterCompletion
        : algorithm::AlgorithmReflectionRefreshMode::EveryTick;
    } else {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector refreshMode must be string or boolean: " + package_json_path.generic_string();
      }
      return false;
    }
  }

  const cJSON* function_name_item = cJSON_GetObjectItemCaseSensitive(reflector, "functionName");
  const std::string default_filter_name =
    function_name_item && cJSON_IsString(function_name_item) && function_name_item->valuestring
      ? function_name_item->valuestring
      : std::string{};

  const int item_count = cJSON_GetArraySize(items);
  for (int i = 0; i < item_count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(items, i);
    if (!item || !cJSON_IsObject(item)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is invalid: " + package_json_path.generic_string();
      }
      return false;
    }

    std::vector<std::string> container_names{};
    std::string reflection_object_name;
    const cJSON* reflect_fun = cJSON_GetObjectItemCaseSensitive(item, "reflectFun");
    const std::string item_filter_name =
      reflect_fun && cJSON_IsString(reflect_fun) && reflect_fun->valuestring
        ? reflect_fun->valuestring
        : default_filter_name;

    if (item_filter_name == "direct") {
      const cJSON* from_item = cJSON_GetObjectItemCaseSensitive(item, "from");
      const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(item, "to");
      const std::vector<std::string> from_names = json_utils::GetStringList(from_item);
      const std::vector<std::string> to_names = json_utils::GetStringList(to_item);
      if (from_names.size() != to_names.size() || from_names.empty()) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package reflector direct item must contain same-length from/to lists: " + package_json_path.generic_string();
        }
        return false;
      }

      for (size_t j = 0; j < from_names.size(); ++j) {
        algorithm::AlgorithmReflectionBinding binding{};
        binding.container_names = {from_names[j]};
        binding.reflection_object_name = to_names[j];
        binding.filter_name = item_filter_name;
        out_reflector->container_bindings_by_reflection_object_name.emplace(
          binding.reflection_object_name,
          binding);
        out_reflector->reflection_objects_by_container_name[binding.container_names.front()].push_back(binding);
      }
      continue;
    }

    const cJSON* input = cJSON_GetObjectItemCaseSensitive(item, "input");
    if (!input || !cJSON_IsObject(input)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing input object: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* varity = cJSON_GetObjectItemCaseSensitive(input, "varity");
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(input, "array");
    if (varity) {
      const std::vector<std::string> varity_names = json_utils::GetStringList(varity);
      container_names.insert(container_names.end(), varity_names.begin(), varity_names.end());
    }
    if (array) {
      const std::vector<std::string> array_names = json_utils::GetStringList(array);
      container_names.insert(container_names.end(), array_names.begin(), array_names.end());
    }
    if (container_names.empty()) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item has an empty input list: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* output = cJSON_GetObjectItemCaseSensitive(item, "output");
    if (!output || !cJSON_IsObject(output)) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing output object: " + package_json_path.generic_string();
      }
      return false;
    }

    const cJSON* output_var = cJSON_GetObjectItemCaseSensitive(output, "v");
    const cJSON* output_arr = cJSON_GetObjectItemCaseSensitive(output, "a");
    const cJSON* output_kind = output_var && cJSON_IsObject(output_var)
      ? output_var
      : (output_arr && cJSON_IsObject(output_arr) ? output_arr : nullptr);
    if (output_kind) {
      const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(output_kind, "name");
      if (name_item && cJSON_IsString(name_item) && name_item->valuestring) {
        reflection_object_name = name_item->valuestring;
      }
    }
    if (reflection_object_name.empty()) {
      cJSON_Delete(root);
      if (out_error_message) {
        *out_error_message = "Package reflector item is missing output name: " + package_json_path.generic_string();
      }
      return false;
    }

    algorithm::AlgorithmReflectionBinding binding{};
    binding.container_names = std::move(container_names);
    binding.reflection_object_name = std::move(reflection_object_name);
    binding.filter_name = std::move(item_filter_name);

    out_reflector->container_bindings_by_reflection_object_name.emplace(
      binding.reflection_object_name,
      binding);
    for (const std::string& container_name : binding.container_names) {
      out_reflector->reflection_objects_by_container_name[container_name].push_back(binding);
    }
  }

  cJSON_Delete(root);
  return true;
}

}  // namespace

bool LoadAlgorithmInterventionFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<agent::IAlgorithmIntervention>* out_intervention,
  std::string* out_error_message) {
  return intervention_detail::LoadAlgorithmInterventionFromLocationImpl(
    package_location,
    out_intervention,
    out_error_message);
}

bool LoadAlgorithmPackageReflectorFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmReflector reflector{};
  std::string reflector_error_message;
  if (!LoadPackageRuntimeReflector(package_location, &reflector, &reflector_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(reflector_error_message);
    }
    return false;
  }

  *out_reflector = std::make_shared<algorithm::AlgorithmReflector>(std::move(reflector));
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_support
