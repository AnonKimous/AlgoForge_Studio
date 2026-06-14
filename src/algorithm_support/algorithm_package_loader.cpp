#include "algorithm_support/algorithm_protocol.h"
#include "algorithm_support/algorithm_package_location.h"
#include "algorithm_support/algorithm_types.h"

#include "cJSON.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <system_error>
#include <utility>

namespace algorithm_support {

namespace {

namespace fs = std::filesystem;

fs::path _BuildPackageJsonPath(const algorithm::AlgorithmPackageLocation& package_location) {
  const std::string algorithm_name = package_location.algorithm_name.empty()
    ? package_location.manifest_name
    : package_location.algorithm_name;
  if (algorithm_name.empty()) {
    return {};
  }

  fs::path package_root = package_location.package_root;
  if (package_root.empty()) {
    package_root = package_location.manifest_path.parent_path();
  }
  if (package_root.empty()) {
    return {};
  }

  return package_root / (algorithm_name + "_package.json");
}

std::string _ReadPackageTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::vector<std::string> _GetStringList(const cJSON* item) {
  std::vector<std::string> values{};
  if (!item) {
    return values;
  }
  if (cJSON_IsString(item) && item->valuestring) {
    values.push_back(item->valuestring);
    return values;
  }
  if (!cJSON_IsArray(item)) {
    return values;
  }
  const int count = cJSON_GetArraySize(item);
  values.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* entry = cJSON_GetArrayItem(item, i);
    if (!entry || !cJSON_IsString(entry) || !entry->valuestring) {
      continue;
    }
    values.emplace_back(entry->valuestring);
  }
  return values;
}

uint32_t _GetUintField(const cJSON* object, const char* key, uint32_t fallback = 0u) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsNumber(item) || item->valuedouble < 0.0) {
    return fallback;
  }
  return static_cast<uint32_t>(item->valuedouble);
}

std::vector<uint32_t> _GetShapeField(const cJSON* object) {
  std::vector<uint32_t> shape{};
  if (!object || !cJSON_IsObject(object)) {
    return shape;
  }

  const cJSON* shape_item = cJSON_GetObjectItemCaseSensitive(object, "shape");
  if (!shape_item || !cJSON_IsArray(shape_item)) {
    return shape;
  }

  const int count = cJSON_GetArraySize(shape_item);
  shape.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* dim = cJSON_GetArrayItem(shape_item, i);
    if (!dim || !cJSON_IsNumber(dim) || dim->valuedouble < 0.0) {
      continue;
    }
    shape.push_back(static_cast<uint32_t>(dim->valuedouble));
  }
  return shape;
}

bool _AppendContainer(
  const std::string& container_name,
  algorithm::AlgorithmContainerStorageKind storage_kind,
  uint32_t element_count,
  uint32_t element_stride,
  algorithm::AlgorithmContainerSet* out_container_set) {
  algorithm::AlgorithmContainer container{};
  container.name = container_name;
  container.storage_kind = storage_kind;
  container.element_count = element_count;
  container.element_stride = element_stride;
  container.bytes.resize(static_cast<size_t>(element_count) * static_cast<size_t>(element_stride));

  switch (storage_kind) {
    case algorithm::AlgorithmContainerStorageKind::Array:
      out_container_set->arrays.push_back(std::move(container));
      break;
    case algorithm::AlgorithmContainerStorageKind::TemporaryRegister:
      out_container_set->temporary_registers.push_back(std::move(container));
      break;
    case algorithm::AlgorithmContainerStorageKind::TemporaryCache:
      out_container_set->temporary_caches.push_back(std::move(container));
      break;
  }
  return true;
}

bool _AppendHiddenInterventionSignalContainer(algorithm::AlgorithmContainerSet* out_container_set) {
  if (!out_container_set) {
    return false;
  }

  algorithm::AlgorithmContainer signal_container{};
  signal_container.name = "__algorithm_intervention_signal";
  signal_container.storage_kind = algorithm::AlgorithmContainerStorageKind::TemporaryCache;
  signal_container.element_count = 1u;
  signal_container.element_stride = sizeof(uint32_t);
  signal_container.hidden = true;
  signal_container.bytes.resize(sizeof(uint32_t));
  out_container_set->hidden_containers.push_back(std::move(signal_container));
  return true;
}

bool _LoadContainerSetFromPackageJson(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmContainerSet>* out_container_set,
  std::string* out_error_message) {
  if (!out_container_set) {
    if (out_error_message) {
      *out_error_message = "Package container set output pointer is null.";
    }
    return false;
  }

  out_container_set->reset();

  const fs::path package_json_path = _BuildPackageJsonPath(package_location);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file.";
    }
    return false;
  }

  const std::string json_text = _ReadPackageTextFile(package_json_path.generic_string());
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

  const cJSON* container_section = cJSON_GetObjectItemCaseSensitive(root, "container");
  if (!container_section || !cJSON_IsObject(container_section)) {
    cJSON_Delete(root);
    if (out_error_message) {
      *out_error_message = "Package JSON is missing container section: " + package_json_path.generic_string();
    }
    return false;
  }

  auto container_set = std::make_shared<algorithm::AlgorithmContainerSet>();
  container_set->algorithm_name = package_location.algorithm_name;

  const cJSON* variable_item = cJSON_GetObjectItemCaseSensitive(container_section, "variable");
  if (cJSON_IsNumber(variable_item)) {
    const uint32_t variable_count = _GetUintField(container_section, "variable");
    for (uint32_t i = 0; i < variable_count; ++i) {
      if (!_AppendContainer("v" + std::to_string(i + 1u), algorithm::AlgorithmContainerStorageKind::TemporaryRegister, 1u, sizeof(float), container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsObject(variable_item)) {
    for (const cJSON* child = variable_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package variable container name is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      const uint32_t element_count = cJSON_IsObject(child) ? std::max<uint32_t>(1u, _GetUintField(child, "count", 1u)) : 1u;
      const std::vector<uint32_t> shape = _GetShapeField(child);
      const uint32_t element_stride = shape.empty()
        ? sizeof(float)
        : static_cast<uint32_t>(shape.size()) * sizeof(float);
      if (!_AppendContainer(child->string, algorithm::AlgorithmContainerStorageKind::TemporaryRegister, element_count, element_stride, container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  }

  const cJSON* variable_array_item = cJSON_GetObjectItemCaseSensitive(container_section, "variableArray");
  if (cJSON_IsNumber(variable_array_item)) {
    const uint32_t variable_array_count = _GetUintField(container_section, "variableArray");
    for (uint32_t i = 0; i < variable_array_count; ++i) {
      if (!_AppendContainer("a" + std::to_string(i + 1u), algorithm::AlgorithmContainerStorageKind::Array, 1u, sizeof(float), container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  } else if (cJSON_IsObject(variable_array_item)) {
    for (const cJSON* child = variable_array_item->child; child; child = child->next) {
      if (!child->string || !*child->string) {
        cJSON_Delete(root);
        if (out_error_message) {
          *out_error_message = "Package array container name is empty: " + package_json_path.generic_string();
        }
        return false;
      }
      const uint32_t element_count = cJSON_IsObject(child) ? std::max<uint32_t>(1u, _GetUintField(child, "count", 1u)) : 1u;
      const std::vector<uint32_t> shape = _GetShapeField(child);
      const uint32_t element_stride = shape.empty()
        ? sizeof(float)
        : static_cast<uint32_t>(shape.size()) * sizeof(float);
      if (!_AppendContainer(child->string, algorithm::AlgorithmContainerStorageKind::Array, element_count, element_stride, container_set.get())) {
        cJSON_Delete(root);
        return false;
      }
    }
  }

  cJSON_Delete(root);
  *out_container_set = std::move(container_set);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _LoadPackageRuntimeReflector(
  const algorithm::AlgorithmPackageLocation& package_location,
  algorithm::AlgorithmReflector* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  const fs::path package_json_path = _BuildPackageJsonPath(package_location);
  if (package_json_path.empty()) {
    if (out_error_message) {
      *out_error_message = "Failed to resolve package JSON file for reflector.";
    }
    return false;
  }

  const std::string json_text = _ReadPackageTextFile(package_json_path.generic_string());
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

    const cJSON* input = cJSON_GetObjectItemCaseSensitive(item, "input");
    const cJSON* input_source = input && cJSON_IsObject(input) ? input : item;
    std::vector<std::string> container_names{};
    const cJSON* varity = cJSON_GetObjectItemCaseSensitive(input_source, "varity");
    const cJSON* array = cJSON_GetObjectItemCaseSensitive(input_source, "array");
    if (varity) {
      const std::vector<std::string> varity_names = _GetStringList(varity);
      container_names.insert(container_names.end(), varity_names.begin(), varity_names.end());
    }
    if (array) {
      const std::vector<std::string> array_names = _GetStringList(array);
      container_names.insert(container_names.end(), array_names.begin(), array_names.end());
    }

    std::string reflection_object_name;
    const cJSON* reflect_fun = cJSON_GetObjectItemCaseSensitive(item, "reflectFun");
    const std::string item_filter_name =
      reflect_fun && cJSON_IsString(reflect_fun) && reflect_fun->valuestring
        ? reflect_fun->valuestring
        : default_filter_name;

    if (item_filter_name == "direct") {
      const cJSON* from_item = cJSON_GetObjectItemCaseSensitive(item, "from");
      const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(item, "to");
      const std::vector<std::string> from_names = _GetStringList(from_item);
      const std::vector<std::string> to_names = _GetStringList(to_item);
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

    const cJSON* output = cJSON_GetObjectItemCaseSensitive(item, "output");
    if (output && cJSON_IsObject(output)) {
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
    }
    if (reflection_object_name.empty()) {
      const cJSON* name_item = cJSON_GetObjectItemCaseSensitive(item, "name");
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

bool CreateAlgorithmPackageContainerSetFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  std::shared_ptr<algorithm::AlgorithmContainerSet>* out_container_set,
  std::string* out_error_message) {
  return _LoadContainerSetFromPackageJson(package_location, out_container_set, out_error_message);
}

bool CreateAlgorithmPackageRuntimeReflectorByName(
  const std::string& algorithm_name,
  std::shared_ptr<algorithm::AlgorithmReflector>* out_reflector,
  std::string* out_error_message) {
  if (!out_reflector) {
    if (out_error_message) {
      *out_error_message = "AlgorithmReflector output pointer is null.";
    }
    return false;
  }

  algorithm::AlgorithmPackageLocation package_location{};
  std::string location_error_message;
  if (!algorithm::TryResolveAlgorithmPackageLocation(
        algorithm_name,
        &package_location,
        &location_error_message)) {
    if (out_error_message) {
      *out_error_message = std::move(location_error_message);
    }
    return false;
  }

  algorithm::AlgorithmReflector reflector{};
  std::string reflector_error_message;
  if (!_LoadPackageRuntimeReflector(package_location, &reflector, &reflector_error_message)) {
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

bool CreateAlgorithmObjectFromLocation(
  const algorithm::AlgorithmPackageLocation& package_location,
  agent::AlgorithmObject* out_group,
  std::string* out_error_message) {
  if (!out_group) {
    if (out_error_message) {
      *out_error_message = "AlgorithmObject output pointer is null.";
    }
    return false;
  }

  if (!package_location.valid) {
    if (out_error_message) {
      *out_error_message = "Algorithm package location is invalid.";
    }
    return false;
  }

  agent::AlgorithmObject group{};
  group.algorithm_profile.algorithm_name = package_location.algorithm_name;
  group.algorithm_profile.container_manifest_name = package_location.manifest_name.empty()
    ? package_location.algorithm_name
    : package_location.manifest_name;

  if (!_LoadContainerSetFromPackageJson(package_location, &group.shared_container_set, out_error_message)) {
    return false;
  }

  AlgorithmPluginComponents plugin_components{};
  std::string plugin_error_message;
  if (package_location.has_plugin_module &&
      TryLoadAlgorithmPluginComponents(package_location, &plugin_components, &plugin_error_message)) {
    group.cpu_symbol = plugin_components.cpu_symbol;
    group.gpu_symbol = plugin_components.gpu_symbol;
    group.intervention = plugin_components.intervention;
    group.temporaryTest_main_thread_executor = plugin_components.temporary_test_executor;
    if (plugin_components.runtime_reflector) {
      group.algorithm_reflector = std::move(plugin_components.runtime_reflector);
    }
    if (!group.algorithm_reflector) {
      std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
      if (CreateAlgorithmPackageRuntimeReflectorByName(
            package_location.algorithm_name,
            &runtime_reflector,
            nullptr)) {
        group.algorithm_reflector = std::move(runtime_reflector);
      }
    }
    if (group.intervention) {
      _AppendHiddenInterventionSignalContainer(group.shared_container_set.get());
    }
    *out_group = std::move(group);
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!plugin_error_message.empty()) {
    if (out_error_message) {
      *out_error_message = std::move(plugin_error_message);
    }
    return false;
  }

  group.cpu_symbol = true;
  group.gpu_symbol = true;
  if (!CreateAlgorithmInterventionByName(
        package_location.algorithm_name,
        &group.intervention,
        out_error_message)) {
    return false;
  }
  if (group.intervention) {
    _AppendHiddenInterventionSignalContainer(group.shared_container_set.get());
  }
  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  if (CreateAlgorithmPackageRuntimeReflectorByName(
        package_location.algorithm_name,
        &runtime_reflector,
        nullptr)) {
    group.algorithm_reflector = std::move(runtime_reflector);
  }

  *out_group = std::move(group);
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace algorithm_support
