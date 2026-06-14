#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "agent/agent.h"
#include "capabilities/algorithm_library/algorithm_plugin_api.h"

#include "cJSON.h"

#include <array>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string_view>

namespace {

struct V6A2DecomposerMeshBinding {
  std::string resource_name;
  std::string container_name;
};

struct V6A2DecomposerDescriptionBinding {
  std::vector<std::string> from_names;
  std::string target_container_name;
  std::vector<uint32_t> target_indices;
  std::vector<std::string> target_container_names;
};

struct V6A2DecomposerDescriptionEntry {
  std::string name;
  std::vector<V6A2DecomposerDescriptionBinding> bindings;
};

struct V6A2DecomposerSchema {
  std::vector<V6A2DecomposerMeshBinding> mesh_bindings;
  std::vector<V6A2DecomposerDescriptionEntry> description_entries;
  bool valid{false};
  std::string error_message;
};

struct V6A2InterventionSchema {
  std::vector<agent::AlgorithmInterventionStageSpec> stage_specs;
  bool valid{false};
  std::string error_message;
};

struct Vec2 {
  float x{0.0f};
  float y{0.0f};
};

constexpr size_t kPackedRenderRecordCount = 7u;
constexpr size_t kPackedRenderRecordFloatCount = 6u;

std::string _ReadTextFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

std::string _GetStringField(const cJSON* object, const char* key) {
  if (!object || !key) {
    return {};
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item || !cJSON_IsString(item) || !item->valuestring) {
    return {};
  }
  return item->valuestring;
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

bool _GetBoolField(const cJSON* object, const char* key, bool fallback = false) {
  if (!object || !key) {
    return fallback;
  }
  const cJSON* item = cJSON_GetObjectItemCaseSensitive(object, key);
  if (!item) {
    return fallback;
  }
  if (item->type & (cJSON_True | cJSON_False)) {
    return (item->type & cJSON_True) != 0;
  }
  return fallback;
}

std::string _BuildPath(
  const algorithm_support::AlgorithmPluginRequest& request,
  const std::string& suffix) {
  const std::string root = request.algorithm_library_root ? request.algorithm_library_root : "";
  const std::string folder = request.algorithm_folder && *request.algorithm_folder
    ? request.algorithm_folder
    : (request.algorithm_name ? request.algorithm_name : "");
  return root + "/" + folder + "/" + folder + suffix;
}

std::vector<std::string> _GetStringList(const cJSON* item) {
  std::vector<std::string> values;
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

std::vector<uint32_t> _GetUintList(const cJSON* item) {
  std::vector<uint32_t> values;
  if (!item || !cJSON_IsArray(item)) {
    return values;
  }

  const int count = cJSON_GetArraySize(item);
  values.reserve(count > 0 ? static_cast<size_t>(count) : 0u);
  for (int i = 0; i < count; ++i) {
    const cJSON* entry = cJSON_GetArrayItem(item, i);
    if (!entry || !cJSON_IsNumber(entry) || entry->valuedouble < 0.0) {
      continue;
    }
    values.push_back(static_cast<uint32_t>(entry->valuedouble));
  }
  return values;
}

bool _WriteFloatAtIndex(AlgorithmContainer* container, uint32_t index, float value) {
  if (!container) {
    return false;
  }
  const size_t byte_offset = static_cast<size_t>(index) * static_cast<size_t>(container->element_stride);
  if (container->bytes.size() < byte_offset + sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, &value, sizeof(float));
  return true;
}

const agent::AlgorithmDescriptorValue* _FindDescriptorValue(
  const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
  std::string_view descriptor_name) {
  for (const agent::AlgorithmDescriptorValue& value : descriptor_values) {
    if (value.descriptor_name == descriptor_name) {
      return &value;
    }
  }
  return nullptr;
}

const agent::AlgorithmResourceBinding* _FindResourceBinding(
  const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
  std::string_view resource_name) {
  for (const agent::AlgorithmResourceBinding& binding : resource_bindings) {
    if (binding.resource_name == resource_name) {
      return &binding;
    }
  }
  return nullptr;
}

bool _ParseInterventionStageKind(
  const std::string& stage_name,
  const std::string& stage_kind_text,
  agent::AlgorithmInterventionStageKind* out_stage_kind) {
  if (!out_stage_kind) {
    return false;
  }

  const std::string kind = !stage_kind_text.empty() ? stage_kind_text : stage_name;
  if (kind == "resultRender") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::ResultRender;
    return true;
  }
  if (kind == "preTick") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::PreExecution;
    return true;
  }
  if (kind == "afterTick") {
    *out_stage_kind = agent::AlgorithmInterventionStageKind::PostExecution;
    return true;
  }

  *out_stage_kind = agent::AlgorithmInterventionStageKind::Custom;
  return true;
}

std::string _TrimLeftCopy(const std::string& text) {
  size_t begin = 0u;
  while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
    ++begin;
  }
  return text.substr(begin);
}

bool _LoadFirstTriangleFromMeshFile(
  const std::string& path,
  std::array<Vec3, 3>* out_triangle,
  std::string* out_error_message) {
  if (!out_triangle) {
    return false;
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (out_error_message) {
      *out_error_message = "Required mesh resource file could not be opened: " + path;
    }
    return false;
  }

  std::vector<Vec3> vertices;
  std::array<int32_t, 3> triangle_indices{};
  bool have_triangle = false;

  std::string line;
  while (std::getline(file, line)) {
    const std::string trimmed = _TrimLeftCopy(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    std::istringstream stream(trimmed);
    char kind = '\0';
    stream >> kind;
    if (!stream) {
      continue;
    }

    if (kind == 'v') {
      Vec3 position{};
      if (!(stream >> position.x >> position.y >> position.z)) {
        if (out_error_message) {
          *out_error_message = "Invalid vertex entry in mesh resource file: " + path;
        }
        return false;
      }
      vertices.push_back(position);
      continue;
    }

    if (kind == 't' && !have_triangle) {
      int32_t a = 0;
      int32_t b = 0;
      int32_t c = 0;
      if (!(stream >> a >> b >> c)) {
        if (out_error_message) {
          *out_error_message = "Invalid triangle entry in mesh resource file: " + path;
        }
        return false;
      }
      triangle_indices = {a, b, c};
      have_triangle = true;
    }
  }

  if (vertices.empty()) {
    if (out_error_message) {
      *out_error_message = "Mesh resource file does not contain any vertices: " + path;
    }
    return false;
  }
  if (!have_triangle) {
    if (out_error_message) {
      *out_error_message = "Mesh resource file does not contain any triangles: " + path;
    }
    return false;
  }

  for (size_t i = 0; i < triangle_indices.size(); ++i) {
    int32_t resolved_index = triangle_indices[i];
    if (resolved_index < 0) {
      resolved_index += static_cast<int32_t>(vertices.size());
    }
    if (resolved_index < 0 || static_cast<size_t>(resolved_index) >= vertices.size()) {
      if (out_error_message) {
        *out_error_message = "Triangle index is out of range in mesh resource file: " + path;
      }
      return false;
    }
    out_triangle->at(i) = vertices[static_cast<size_t>(resolved_index)];
  }

  return true;
}

bool _ReadPackedRenderRecord(
  const AlgorithmContainer* container,
  size_t record_index,
  float out_record[kPackedRenderRecordFloatCount]) {
  if (!container || !out_record) {
    return false;
  }

  const size_t byte_offset = record_index * kPackedRenderRecordFloatCount * sizeof(float);
  const size_t record_size = kPackedRenderRecordFloatCount * sizeof(float);
  if (container->bytes.size() < byte_offset + record_size) {
    return false;
  }
  std::memcpy(out_record, container->bytes.data() + byte_offset, record_size);
  return true;
}

bool _WritePackedRenderRecord(
  AlgorithmContainer* container,
  size_t record_index,
  const float record[kPackedRenderRecordFloatCount]) {
  if (!container || !record) {
    return false;
  }

  const size_t byte_offset = record_index * kPackedRenderRecordFloatCount * sizeof(float);
  const size_t record_size = kPackedRenderRecordFloatCount * sizeof(float);
  if (container->bytes.size() < byte_offset + record_size) {
    return false;
  }
  std::memcpy(container->bytes.data() + byte_offset, record, record_size);
  return true;
}

bool _ReadPackedTriangle(
  const AlgorithmContainer* container,
  std::array<Vec3, 3>* out_triangle) {
  if (!container || !out_triangle) {
    return false;
  }

  for (size_t i = 0; i < out_triangle->size(); ++i) {
    float record[kPackedRenderRecordFloatCount]{};
    if (!_ReadPackedRenderRecord(container, 1u + i, record)) {
      return false;
    }
    out_triangle->at(i) = Vec3{record[0], record[1], record[2]};
  }
  return true;
}

bool _WritePackedRenderData(
  AlgorithmContainer* container,
  const Vec2& point_pos,
  const std::array<Vec3, 3>& triangle) {
  if (!container) {
    return false;
  }
  if (container->bytes.size() < kPackedRenderRecordCount * kPackedRenderRecordFloatCount * sizeof(float)) {
    return false;
  }

  const float point_record[kPackedRenderRecordFloatCount]{
    point_pos.x, point_pos.y, 0.0f,
    point_pos.x, point_pos.y, 0.0f,
  };
  const float vertex_a_record[kPackedRenderRecordFloatCount]{
    triangle[0].x, triangle[0].y, triangle[0].z,
    triangle[0].x, triangle[0].y, triangle[0].z,
  };
  const float vertex_b_record[kPackedRenderRecordFloatCount]{
    triangle[1].x, triangle[1].y, triangle[1].z,
    triangle[1].x, triangle[1].y, triangle[1].z,
  };
  const float vertex_c_record[kPackedRenderRecordFloatCount]{
    triangle[2].x, triangle[2].y, triangle[2].z,
    triangle[2].x, triangle[2].y, triangle[2].z,
  };
  const float edge_ab_record[kPackedRenderRecordFloatCount]{
    triangle[0].x, triangle[0].y, triangle[0].z,
    triangle[1].x, triangle[1].y, triangle[1].z,
  };
  const float edge_bc_record[kPackedRenderRecordFloatCount]{
    triangle[1].x, triangle[1].y, triangle[1].z,
    triangle[2].x, triangle[2].y, triangle[2].z,
  };
  const float edge_ca_record[kPackedRenderRecordFloatCount]{
    triangle[2].x, triangle[2].y, triangle[2].z,
    triangle[0].x, triangle[0].y, triangle[0].z,
  };

  return _WritePackedRenderRecord(container, 0u, point_record) &&
    _WritePackedRenderRecord(container, 1u, vertex_a_record) &&
    _WritePackedRenderRecord(container, 2u, vertex_b_record) &&
    _WritePackedRenderRecord(container, 3u, vertex_c_record) &&
    _WritePackedRenderRecord(container, 4u, edge_ab_record) &&
    _WritePackedRenderRecord(container, 5u, edge_bc_record) &&
    _WritePackedRenderRecord(container, 6u, edge_ca_record);
}

void _LoadContainerBindings(
  const cJSON* stage_item,
  const char* list_name,
  const std::string& container_kind,
  std::vector<agent::AlgorithmInterventionContainerBinding>* out_bindings,
  std::string* out_error_message,
  const std::string& path,
  uint32_t default_tuple_width) {
  if (!stage_item || !list_name || !out_bindings) {
    return;
  }

  const cJSON* bindings = cJSON_GetObjectItemCaseSensitive(stage_item, list_name);
  if (!bindings || !cJSON_IsArray(bindings)) {
    return;
  }

  const int count = cJSON_GetArraySize(bindings);
  out_bindings->reserve(out_bindings->size() + (count > 0 ? static_cast<size_t>(count) : 0u));
  for (int i = 0; i < count; ++i) {
    const cJSON* item = cJSON_GetArrayItem(bindings, i);
    if (!item) {
      continue;
    }

    agent::AlgorithmInterventionContainerBinding binding{};
    binding.container_kind = container_kind;
    binding.tuple_width = default_tuple_width;
    binding.required = true;
    if (cJSON_IsString(item) && item->valuestring) {
      binding.container_name = item->valuestring;
    } else if (cJSON_IsObject(item)) {
      binding.container_name = _GetStringField(item, "name");
      if (binding.container_name.empty()) {
        binding.container_name = _GetStringField(item, "container");
      }
      const std::string kind = _GetStringField(item, "kind");
      if (!kind.empty()) {
        binding.container_kind = kind;
      }
      binding.tuple_width = _GetUintField(item, "tuple_width", default_tuple_width);
      if (binding.tuple_width == 0u) {
        binding.tuple_width = default_tuple_width;
      }
      binding.required = _GetBoolField(item, "required", true);
    }

    if (binding.container_name.empty()) {
      if (out_error_message) {
        *out_error_message = "Invalid " + std::string(list_name) + " binding in intervention JSON file: " + path;
      }
      return;
    }
    out_bindings->push_back(std::move(binding));
  }
}

V6A2InterventionSchema _LoadInterventionSchema(const algorithm_support::AlgorithmPluginRequest& request) {
  V6A2InterventionSchema schema{};
  const std::string path = _BuildPath(request, "_package.json");
  const std::string json_text = _ReadTextFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + path;
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + path;
    return schema;
  }

  const cJSON* intervention = cJSON_GetObjectItemCaseSensitive(root, "intervention");
  if (!intervention || !cJSON_IsObject(intervention)) {
    schema.error_message = "Package JSON file " + path + " is missing an 'intervention' object.";
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* stages = cJSON_GetObjectItemCaseSensitive(intervention, "stages");
  if (!stages || !cJSON_IsObject(stages)) {
    schema.error_message = "Intervention section in package JSON file " + path + " is missing a 'stages' object.";
    cJSON_Delete(root);
    return schema;
  }

  for (const cJSON* stage_item = stages->child; stage_item; stage_item = stage_item->next) {
    if (!stage_item || !cJSON_IsObject(stage_item) || !stage_item->string) {
      continue;
    }

    const std::string stage_key = stage_item->string;
    agent::AlgorithmInterventionStageSpec stage_spec{};
    stage_spec.stage_name = _GetStringField(stage_item, "stage_name");
    if (stage_spec.stage_name.empty()) {
      stage_spec.stage_name = stage_key;
    }

    const std::string stage_kind_text = _GetStringField(stage_item, "stage_kind");
    if (!_ParseInterventionStageKind(stage_spec.stage_name, stage_kind_text, &stage_spec.stage_kind)) {
      schema.error_message = "Invalid stage kind in package JSON file: " + path;
      cJSON_Delete(root);
      return schema;
    }

    _LoadContainerBindings(
      stage_item,
      "variables",
      "variable",
      &stage_spec.used_algorithm_containers,
      &schema.error_message,
      path,
      1u);
    if (!schema.error_message.empty()) {
      cJSON_Delete(root);
      return schema;
    }
    _LoadContainerBindings(
      stage_item,
      "arrays",
      "array",
      &stage_spec.used_algorithm_containers,
      &schema.error_message,
      path,
      2u);
    if (!schema.error_message.empty()) {
      cJSON_Delete(root);
      return schema;
    }

    const cJSON* shader = cJSON_GetObjectItemCaseSensitive(stage_item, "shader");
    if (shader && cJSON_IsObject(shader)) {
      stage_spec.shader.vertex_shader_path = _GetStringField(shader, "vertex");
      stage_spec.shader.fragment_shader_path = _GetStringField(shader, "fragment");
      stage_spec.shader.pipeline_kind = _GetStringField(shader, "pipeline");
    }

    if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::ResultRender) {
      if (stage_spec.used_algorithm_containers.empty()) {
        schema.error_message = "Result-render stage in package JSON file must bind at least one container: " + path;
        cJSON_Delete(root);
        return schema;
      }
      if (stage_spec.shader.vertex_shader_path.empty() || stage_spec.shader.fragment_shader_path.empty()) {
        schema.error_message = "Result-render stage in package JSON file is missing shader paths: " + path;
        cJSON_Delete(root);
        return schema;
      }
    } else if (stage_spec.stage_kind == agent::AlgorithmInterventionStageKind::PostExecution) {
      if (stage_spec.used_algorithm_containers.empty()) {
        schema.error_message = "Post-execution stage in package JSON file must bind containers: " + path;
        cJSON_Delete(root);
        return schema;
      }
    }

    schema.stage_specs.push_back(std::move(stage_spec));
  }

  cJSON_Delete(root);
  schema.valid = !schema.stage_specs.empty();
  if (!schema.valid && schema.error_message.empty()) {
    schema.error_message = path + " does not contain any intervention stages.";
  }
  return schema;
}

V6A2DecomposerSchema _LoadDecomposerSchema(const algorithm_support::AlgorithmPluginRequest& request) {
  V6A2DecomposerSchema schema{};
  const std::string path = _BuildPath(request, "_package.json");
  const std::string json_text = _ReadTextFile(path);
  if (json_text.empty()) {
    schema.error_message = "Failed to read package JSON file: " + path;
    return schema;
  }

  cJSON* root = cJSON_Parse(json_text.c_str());
  if (!root) {
    schema.error_message = "Failed to parse package JSON file: " + path;
    return schema;
  }

  const std::string algorithm_name = _GetStringField(root, "algorithm_name");
  if (algorithm_name.empty()) {
    schema.error_message = "Decomposer JSON file is missing algorithm_name: " + path;
    cJSON_Delete(root);
    return schema;
  }
  if (request.algorithm_name && algorithm_name != request.algorithm_name) {
    schema.error_message = "Decomposer JSON file algorithm_name does not match request: " + path;
    cJSON_Delete(root);
    return schema;
  }

  const cJSON* mesh = cJSON_GetObjectItemCaseSensitive(root, "mesh");
  if (!mesh || !cJSON_IsObject(mesh)) {
    schema.error_message = "Decomposer JSON file is missing mesh bindings: " + path;
    cJSON_Delete(root);
    return schema;
  }

  for (const cJSON* child = mesh->child; child; child = child->next) {
    if (!child->string || !*child->string || !cJSON_IsString(child) || !child->valuestring) {
      schema.error_message = "Invalid mesh binding in decomposer JSON file: " + path;
      cJSON_Delete(root);
      return schema;
    }
    schema.mesh_bindings.push_back(V6A2DecomposerMeshBinding{
      .resource_name = child->string,
      .container_name = child->valuestring,
    });
  }

  const cJSON* description = cJSON_GetObjectItemCaseSensitive(root, "description");
  if (!description || !cJSON_IsArray(description)) {
    schema.error_message = "Decomposer JSON file is missing description entries: " + path;
    cJSON_Delete(root);
    return schema;
  }

  const int description_count = cJSON_GetArraySize(description);
  schema.description_entries.reserve(description_count > 0 ? static_cast<size_t>(description_count) : 0u);
  for (int i = 0; i < description_count; ++i) {
    const cJSON* entry_item = cJSON_GetArrayItem(description, i);
    if (!entry_item || !cJSON_IsObject(entry_item)) {
      schema.error_message = "Invalid description entry in decomposer JSON file: " + path;
      cJSON_Delete(root);
      return schema;
    }

    V6A2DecomposerDescriptionEntry entry{};
    entry.name = _GetStringField(entry_item, "name");
    if (entry.name.empty()) {
      schema.error_message = "Description entry is missing name: " + path;
      cJSON_Delete(root);
      return schema;
    }

    const cJSON* bindings = cJSON_GetObjectItemCaseSensitive(entry_item, "bindings");
    if (!bindings || !cJSON_IsArray(bindings)) {
      schema.error_message = "Description entry is missing bindings: " + path;
      cJSON_Delete(root);
      return schema;
    }

    const int binding_count = cJSON_GetArraySize(bindings);
    entry.bindings.reserve(binding_count > 0 ? static_cast<size_t>(binding_count) : 0u);
    for (int j = 0; j < binding_count; ++j) {
      const cJSON* binding_item = cJSON_GetArrayItem(bindings, j);
      if (!binding_item || !cJSON_IsObject(binding_item)) {
        schema.error_message = "Invalid description binding in decomposer JSON file: " + path;
        cJSON_Delete(root);
        return schema;
      }

      V6A2DecomposerDescriptionBinding binding{};
      const cJSON* from_item = cJSON_GetObjectItemCaseSensitive(binding_item, "from");
      binding.from_names = _GetStringList(from_item);
      if (binding.from_names.empty()) {
        schema.error_message = "Description binding is missing from names: " + path;
        cJSON_Delete(root);
        return schema;
      }

      const cJSON* to_item = cJSON_GetObjectItemCaseSensitive(binding_item, "to");
      if (cJSON_IsArray(to_item)) {
        binding.target_container_names = _GetStringList(to_item);
        if (binding.target_container_names.empty()) {
          schema.error_message = "Description binding has empty to list: " + path;
          cJSON_Delete(root);
          return schema;
        }
      } else if (cJSON_IsObject(to_item)) {
        binding.target_container_name = _GetStringField(to_item, "container");
        binding.target_indices = _GetUintList(cJSON_GetObjectItemCaseSensitive(to_item, "indices"));
        if (binding.target_container_name.empty() || binding.target_indices.empty()) {
          schema.error_message = "Description binding has invalid indexed target: " + path;
          cJSON_Delete(root);
          return schema;
        }
      } else {
        schema.error_message = "Description binding is missing to mapping: " + path;
        cJSON_Delete(root);
        return schema;
      }

      entry.bindings.push_back(std::move(binding));
    }

    schema.description_entries.push_back(std::move(entry));
  }

  cJSON_Delete(root);
  schema.valid = !schema.mesh_bindings.empty() && !schema.description_entries.empty();
  if (!schema.valid) {
    schema.error_message = "Decomposer JSON file does not contain mesh and description entries: " + path;
  }
  return schema;
}

bool _ReadFloat(const AlgorithmContainer* container, float* out_value) {
  if (!container || !out_value || container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(out_value, container->bytes.data(), sizeof(float));
  return true;
}

bool _WriteFloat(AlgorithmContainer* container, float value) {
  if (!container || container->bytes.size() < sizeof(float)) {
    return false;
  }
  std::memcpy(container->bytes.data(), &value, sizeof(float));
  return true;
}

bool _ReadVec2(const AlgorithmContainer* container, Vec2* out_value) {
  if (!container || !out_value || container->bytes.size() < sizeof(float) * 2u) {
    return false;
  }
  std::memcpy(out_value, container->bytes.data(), sizeof(float) * 2u);
  return true;
}

bool _WriteVec2(AlgorithmContainer* container, const Vec2& value) {
  if (!container || container->bytes.size() < sizeof(float) * 2u) {
    return false;
  }
  std::memcpy(container->bytes.data(), &value, sizeof(float) * 2u);
  return true;
}

bool _ReadTriangle(const AlgorithmContainer* container, float out_triangle[6]) {
  if (!container || !out_triangle || container->bytes.size() < sizeof(float) * 6u) {
    return false;
  }
  std::memcpy(out_triangle, container->bytes.data(), sizeof(float) * 6u);
  return true;
}

bool _WriteTriangle(AlgorithmContainer* container, const float triangle[6]) {
  if (!container || !triangle || container->bytes.size() < sizeof(float) * 6u) {
    return false;
  }
  std::memcpy(container->bytes.data(), triangle, sizeof(float) * 6u);
  return true;
}

Vec2 _Add(const Vec2& lhs, const Vec2& rhs) {
  return Vec2{lhs.x + rhs.x, lhs.y + rhs.y};
}

Vec2 _Sub(const Vec2& lhs, const Vec2& rhs) {
  return Vec2{lhs.x - rhs.x, lhs.y - rhs.y};
}

float _Cross(const Vec2& origin, const Vec2& a, const Vec2& b) {
  const Vec2 u = _Sub(a, origin);
  const Vec2 v = _Sub(b, origin);
  return u.x * v.y - u.y * v.x;
}

bool _PointInTriangle(const Vec2& point, const float triangle[6]) {
  const Vec2 a{triangle[0], triangle[1]};
  const Vec2 b{triangle[2], triangle[3]};
  const Vec2 c{triangle[4], triangle[5]};
  const float d1 = _Cross(point, a, b);
  const float d2 = _Cross(point, b, c);
  const float d3 = _Cross(point, c, a);
  const float eps = 1e-6f;
  const bool has_negative = (d1 < -eps) || (d2 < -eps) || (d3 < -eps);
  const bool has_positive = (d1 > eps) || (d2 > eps) || (d3 > eps);
  return !(has_negative && has_positive);
}

bool _IsZeroVector(const Vec2& value) {
  return std::fabs(value.x) < 1e-6f && std::fabs(value.y) < 1e-6f;
}

bool _IsZeroTriangle(const float triangle[6]) {
  for (size_t i = 0; i < 6u; ++i) {
    if (std::fabs(triangle[i]) >= 1e-6f) {
      return false;
    }
  }
  return true;
}

void _SeedDefaults(
  AlgorithmContainer* point_pos_x,
  AlgorithmContainer* point_pos_y,
  AlgorithmContainer* point_vel_x,
  AlgorithmContainer* point_vel_y,
  AlgorithmContainer* collision_count,
  AlgorithmContainer* collision_latched,
  AlgorithmContainer* triangle_velocity) {
  const float pos_x = 0.0f;
  const float pos_y = 0.15f;
  const float vel_x = 0.06f;
  const float vel_y = 0.0f;
  const Vec2 tri_velocity{0.0f, 0.0f};
  const float zero = 0.0f;
  _WriteFloat(point_pos_x, pos_x);
  _WriteFloat(point_pos_y, pos_y);
  _WriteFloat(point_vel_x, vel_x);
  _WriteFloat(point_vel_y, vel_y);
  _WriteFloat(collision_count, zero);
  _WriteFloat(collision_latched, zero);
  _WriteVec2(triangle_velocity, tri_velocity);
}

class V6A2TriangleCollisionDecomposer final {
 public:
  bool GetRequestedResources(
    const algorithm::AlgorithmProfile& algorithm_profile,
    agent::AlgorithmRequestedResources* out_requested_resources) const {
    if (!out_requested_resources) {
      return false;
    }
    out_requested_resources->algorithm_name = algorithm_profile.algorithm_name;
    out_requested_resources->required_resources.clear();
    if (!schema_.valid) {
      return false;
    }
    out_requested_resources->required_resources.reserve(schema_.mesh_bindings.size());
    for (const V6A2DecomposerMeshBinding& binding : schema_.mesh_bindings) {
      out_requested_resources->required_resources.push_back(agent::AlgorithmRequestedResources::RequiredResource{
        .resource_name = binding.resource_name,
        .resource_kind = "mesh",
        .required = true,
      });
    }
    out_requested_resources->valid = !out_requested_resources->required_resources.empty();
    return true;
  }

  bool GetRequestedDescriptorBindings(
    const algorithm::AlgorithmProfile& algorithm_profile,
    agent::AlgorithmRequestedDescriptorBindings* out_requested_descriptor_bindings) const {
    if (!out_requested_descriptor_bindings) {
      return false;
    }
    out_requested_descriptor_bindings->algorithm_name = algorithm_profile.algorithm_name;
    out_requested_descriptor_bindings->descriptor_slots.clear();
    if (!schema_.valid) {
      return false;
    }

    for (const V6A2DecomposerDescriptionEntry& entry : schema_.description_entries) {
      for (const V6A2DecomposerDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          if (binding.from_names.size() != binding.target_container_names.size() &&
              binding.from_names.size() != 1u) {
            return false;
          }

          for (size_t i = 0; i < binding.target_container_names.size(); ++i) {
            const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
            out_requested_descriptor_bindings->descriptor_slots.push_back(
              agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
                .descriptor_name = binding.from_names[from_index],
                .container_name = binding.target_container_names[i],
                .array_index = 0u,
              });
          }
          continue;
        }

        if (binding.from_names.size() != binding.target_indices.size() &&
            binding.from_names.size() != 1u) {
          return false;
        }

        for (size_t i = 0; i < binding.target_indices.size(); ++i) {
          const uint32_t target_index = binding.target_indices[i];
          if (target_index == 0u) {
            return false;
          }

          const size_t from_index = binding.from_names.size() == 1u ? 0u : i;
          out_requested_descriptor_bindings->descriptor_slots.push_back(
            agent::AlgorithmRequestedDescriptorBindings::DescriptorSlot{
              .descriptor_name = binding.from_names[from_index],
              .container_name = binding.target_container_name,
              .array_index = target_index - 1u,
            });
        }
      }
    }
    out_requested_descriptor_bindings->valid = !out_requested_descriptor_bindings->descriptor_slots.empty();
    return true;
  }

  bool Decompose(
    const algorithm::AlgorithmProfile& algorithm_profile,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    algorithm::AlgorithmContainerSet* container_set,
    std::string* out_error_message = nullptr) const {
    if (!container_set) {
      if (out_error_message) {
        *out_error_message = "AlgorithmContainerSet output pointer is null.";
      }
      return false;
    }

    if (!schema_.valid) {
      if (out_error_message) {
        *out_error_message = schema_.error_message;
      }
      return false;
    }

    for (const V6A2DecomposerMeshBinding& binding : schema_.mesh_bindings) {
      const agent::AlgorithmResourceBinding* resource_binding =
        _FindResourceBinding(resource_bindings, binding.resource_name);
      if (!resource_binding) {
        _SetErrorMessage(
          out_error_message,
          "Missing required resource binding '" + binding.resource_name + "' for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      if (resource_binding->source_path.empty()) {
        _SetErrorMessage(
          out_error_message,
          "Required resource '" + binding.resource_name + "' has no source path for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
      std::ifstream file(resource_binding->source_path, std::ios::binary);
      if (!file) {
        _SetErrorMessage(
          out_error_message,
          "Required resource file '" + resource_binding->source_path + "' could not be opened for '" +
            algorithm_profile.algorithm_name + "'.");
        return false;
      }
    }

    for (const V6A2DecomposerDescriptionEntry& entry : schema_.description_entries) {
      for (const V6A2DecomposerDescriptionBinding& binding : entry.bindings) {
        if (!binding.target_container_names.empty()) {
          const size_t from_count = binding.from_names.size();
          const size_t target_count = binding.target_container_names.size();
          if (from_count != target_count && from_count != 1u) {
            _SetErrorMessage(
              out_error_message,
              "Description entry '" + entry.name + "' has mismatched from/to list sizes for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }

          for (size_t i = 0; i < target_count; ++i) {
            const size_t from_index = from_count == 1u ? 0u : i;
            const agent::AlgorithmDescriptorValue* value =
              _FindDescriptorValue(descriptor_values, binding.from_names[from_index]);
            if (!value) {
              _SetErrorMessage(
                out_error_message,
                "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
                  algorithm_profile.algorithm_name + "'.");
              return false;
            }

            algorithm::AlgorithmContainer* container =
              FindAlgorithmContainer(container_set, binding.target_container_names[i]);
            if (!container) {
              _SetErrorMessage(
                out_error_message,
                "Missing target container '" + binding.target_container_names[i] + "' for '" +
                  algorithm_profile.algorithm_name + "'.");
              return false;
            }

            if (!_WriteFloatAtIndex(container, 0u, value->scalar_value)) {
              _SetErrorMessage(
                out_error_message,
                "Failed to write descriptor value into container '" + binding.target_container_names[i] + "' for '" +
                  algorithm_profile.algorithm_name + "'.");
              return false;
            }
          }
          continue;
        }

        const size_t from_count = binding.from_names.size();
        const size_t index_count = binding.target_indices.size();
        if (from_count != index_count && from_count != 1u) {
          _SetErrorMessage(
            out_error_message,
            "Description entry '" + entry.name + "' has mismatched source count and index count for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }

        algorithm::AlgorithmContainer* container = FindAlgorithmContainer(container_set, binding.target_container_name);
        if (!container) {
          _SetErrorMessage(
            out_error_message,
            "Missing target container '" + binding.target_container_name + "' for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }
        if (container->storage_kind != AlgorithmContainerStorageKind::Array) {
          _SetErrorMessage(
            out_error_message,
            "Target container '" + binding.target_container_name + "' is not an array for '" +
              algorithm_profile.algorithm_name + "'.");
          return false;
        }

        for (size_t i = 0; i < index_count; ++i) {
          const uint32_t target_index = binding.target_indices[i];
          if (target_index == 0u) {
            _SetErrorMessage(
              out_error_message,
              "Description entry '" + entry.name + "' uses an invalid container index for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }

          const size_t from_index = from_count == 1u ? 0u : i;
          const agent::AlgorithmDescriptorValue* value =
            _FindDescriptorValue(descriptor_values, binding.from_names[from_index]);
          if (!value) {
            _SetErrorMessage(
              out_error_message,
              "Missing descriptor value '" + binding.from_names[from_index] + "' for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }

          if (!_WriteFloatAtIndex(container, target_index - 1u, value->scalar_value)) {
            _SetErrorMessage(
              out_error_message,
              "Failed to write descriptor value into container '" + binding.target_container_name + "' for '" +
                algorithm_profile.algorithm_name + "'.");
            return false;
          }
        }
      }
    }

    return true;
  }
};

class V6A2TriangleCollisionIntervention final : public agent::IAlgorithmIntervention {
 public:
  explicit V6A2TriangleCollisionIntervention(V6A2InterventionSchema schema)
    : schema_(std::move(schema)) {}

  bool SupportsIntervention() const override {
    return schema_.valid && !schema_.stage_specs.empty();
  }

  void FillAgentToAlgorithmSignal(
    const agent::AgentTickContext& context,
    AgentToAlgorithmSignal* out_signal) const override {
    if (!out_signal) {
      return;
    }

    *out_signal = {};
    out_signal->needs_intervention = context.intervention_request && context.intervention_request->enabled;
  }

  bool GetInterventionStageSpecs(
    std::vector<agent::AlgorithmInterventionStageSpec>* out_stage_specs) const override {
    if (!out_stage_specs) {
      return false;
    }
    *out_stage_specs = schema_.stage_specs;
    return schema_.valid && !schema_.stage_specs.empty();
  }

 private:
  V6A2InterventionSchema schema_{};
};

class V6A2TriangleCollisionMainThreadExecutor final : public agent::IAlgorithmtemporaryTestMainThreadExecutor {
 public:
  bool temporaryTestExecuteOnMainThread(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    if (!algorithm_container_set) {
      return false;
    }

    AlgorithmContainer* point_pos_x = FindAlgorithmContainer(algorithm_container_set, "v1");
    AlgorithmContainer* point_pos_y = FindAlgorithmContainer(algorithm_container_set, "v2");
    AlgorithmContainer* point_vel_x = FindAlgorithmContainer(algorithm_container_set, "v3");
    AlgorithmContainer* point_vel_y = FindAlgorithmContainer(algorithm_container_set, "v4");
    AlgorithmContainer* collision_count = FindAlgorithmContainer(algorithm_container_set, "v5");
    AlgorithmContainer* collision_latched = FindAlgorithmContainer(algorithm_container_set, "v6");
    AlgorithmContainer* triangle_render = FindAlgorithmContainer(algorithm_container_set, "a1");
    AlgorithmContainer* triangle_velocity = FindAlgorithmContainer(algorithm_container_set, "a2");
    if (!point_pos_x || !point_pos_y || !point_vel_x || !point_vel_y ||
        !collision_count || !collision_latched || !triangle_render || !triangle_velocity) {
      return false;
    }

    Vec2 point_pos{};
    Vec2 point_vel{};
    Vec2 tri_vel{};
    std::array<Vec3, 3> triangle_vertices{};
    if (!_ReadFloat(point_pos_x, &point_pos.x) ||
        !_ReadFloat(point_pos_y, &point_pos.y) ||
        !_ReadFloat(point_vel_x, &point_vel.x) ||
        !_ReadFloat(point_vel_y, &point_vel.y) ||
        !_ReadVec2(triangle_velocity, &tri_vel) ||
        !_ReadPackedTriangle(triangle_render, &triangle_vertices)) {
      return false;
    }

    float count_value = 0.0f;
    float latched_value = 0.0f;
    if (!_ReadFloat(collision_count, &count_value) ||
        !_ReadFloat(collision_latched, &latched_value)) {
      return false;
    }

    if (_IsZeroVector(point_pos) && _IsZeroVector(point_vel) && _IsZeroVector(tri_vel)) {
      _SeedDefaults(
        point_pos_x,
        point_pos_y,
        point_vel_x,
        point_vel_y,
        collision_count,
        collision_latched,
        triangle_velocity);
      if (!_ReadFloat(point_pos_x, &point_pos.x) ||
          !_ReadFloat(point_pos_y, &point_pos.y) ||
          !_ReadFloat(point_vel_x, &point_vel.x) ||
          !_ReadFloat(point_vel_y, &point_vel.y) ||
          !_ReadVec2(triangle_velocity, &tri_vel) ||
          !_ReadPackedTriangle(triangle_render, &triangle_vertices)) {
        return false;
      }
      count_value = 0.0f;
      latched_value = 0.0f;
    }

    const bool already_collided = latched_value >= 0.5f;
    if (!already_collided) {
      point_pos = _Add(point_pos, point_vel);
    }

    for (size_t i = 0; i < 3u; ++i) {
      triangle_vertices[i].x += tri_vel.x;
      triangle_vertices[i].y += tri_vel.y;
    }

    const float triangle_points[6]{
      triangle_vertices[0].x, triangle_vertices[0].y,
      triangle_vertices[1].x, triangle_vertices[1].y,
      triangle_vertices[2].x, triangle_vertices[2].y,
    };

    const bool inside_triangle = _PointInTriangle(point_pos, triangle_points);
    if (inside_triangle && !already_collided) {
      tri_vel.x += point_vel.x;
      tri_vel.y += point_vel.y;
      point_vel.x = 0.0f;
      point_vel.y = 0.0f;
      count_value += 1.0f;
      latched_value = 1.0f;
    }

    if (!_WriteFloat(point_pos_x, point_pos.x) ||
        !_WriteFloat(point_pos_y, point_pos.y) ||
        !_WriteFloat(point_vel_x, point_vel.x) ||
        !_WriteFloat(point_vel_y, point_vel.y) ||
        !_WriteFloat(collision_count, count_value) ||
        !_WriteFloat(collision_latched, latched_value) ||
        !_WritePackedRenderData(triangle_render, point_pos, triangle_vertices) ||
        !_WriteVec2(triangle_velocity, tri_vel)) {
      return false;
    }

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
      algorithm_to_agent_signal->intervention_applied = inside_triangle;
      algorithm_to_agent_signal->intervention_needed = !inside_triangle && !already_collided;
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_support::AdvancedAlgorithmDebugSignal{
        .name = "v6a2_triangle_collision_runtime_test.body",
        .payload = inside_triangle
          ? "Collision detected: vertex stopped and triangle absorbed the vertex velocity."
          : "Algorithm body advanced without collision.",
      });
    }
    return true;
  }
};

void _DestroyIntervention(agent::IAlgorithmIntervention* intervention) {
  delete intervention;
}

void _DestroyTemporaryTestExecutor(agent::IAlgorithmtemporaryTestMainThreadExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithm_support::AlgorithmPluginRequest* request,
  algorithm_support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();

  const V6A2InterventionSchema intervention_schema = _LoadInterventionSchema(*request);
  if (!intervention_schema.valid) {
    return false;
  }

  out_bundle->intervention = new V6A2TriangleCollisionIntervention(intervention_schema);
  out_bundle->destroy_intervention = &_DestroyIntervention;
  out_bundle->temporary_test_executor = new V6A2TriangleCollisionMainThreadExecutor();
  out_bundle->destroy_temporary_test_executor = &_DestroyTemporaryTestExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithm_support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  if (!algorithm_management::CreateAlgorithmPackageRuntimeReflectorByName(
        request->algorithm_name ? request->algorithm_name : "",
        &runtime_reflector,
        nullptr)) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
