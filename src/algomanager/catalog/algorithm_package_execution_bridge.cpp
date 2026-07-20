#include "algomanager/bridge/algorithm_protocol.h"
#include "algomanager/catalog/algorithm_vk_exec_support_detail.h"
#include "algomanager/catalog/algorithm_intervention_support_detail.h"
#include "algomanager/bridge/algorithm_package_location.h"
#include "algomanager/catalog/algorithm_package_paths.h"
#include "algomanager/bridge/algorithm_types.h"
#include "algomanager/catalog/algorithm_json_utils.h"
#define RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "runtimesys/runtime_environment.h"
#undef RUNTIME_SYSTEMS_LAYER_PUBLIC_FACADE_INCLUDE

#include "cJSON.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace algomanager {

namespace bridge { namespace execution_bridge_support {

std::string ResolveAlgorithmVkShaderPath(
  const ::algomanager::bridge::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_error_message) {
  if (shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "VK shader path must not be empty.";
    }
    return {};
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return path.string();
  }

  std::filesystem::path runtime_package_root(object.runtime_package_root_path);
  if (runtime_package_root.empty()) {
    ::algorithm::AlgorithmPackageLocation package_location{};
    std::string resolve_error_message;
    if (!::algorithm::TryResolveAlgorithmPackageLocation(
          object.algorithm_profile.algorithm_name,
          &package_location,
          &resolve_error_message)) {
      if (out_error_message) {
        *out_error_message = resolve_error_message.empty()
          ? ("Failed to resolve algorithm package location for '" + object.algorithm_profile.algorithm_name + "'.")
          : std::move(resolve_error_message);
      }
      return {};
    }
    runtime_package_root = package_location.runtime_package_root;
  }
  if (runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Algorithm runtime package root is empty for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return {};
  }

  const std::filesystem::path resolved_path = (runtime_package_root / path).lexically_normal();
  if (resolved_path.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Failed to resolve VK shader path for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return {};
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return resolved_path.string();
}

bool TryBuildAlgorithmInterventionVkPhaseSubJob(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AlgorithmPhaseSpec& phase_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message) {
  if (!container_set || !out_stage_job) {
    if (out_error_message) {
      *out_error_message = "VK stage sub-job output pointer is null.";
    }
    return false;
  }
  if (phase_spec.shader.vertex_shader_path.empty() || phase_spec.shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "VK phase is missing shader paths.";
    }
    return false;
  }
  if (phase_spec.used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "VK phase does not bind any containers.";
    }
    return false;
  }

  out_stage_job->debug_name = object.algorithm_profile.algorithm_name + "::" + phase_spec.stage_name;
  out_stage_job->stage_name = phase_spec.stage_name;
  out_stage_job->vertex_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    phase_spec.shader.vertex_shader_path,
    out_error_message);
  if (out_stage_job->vertex_shader_path.empty()) {
    return false;
  }
  out_stage_job->fragment_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    phase_spec.shader.fragment_shader_path,
    out_error_message);
  if (out_stage_job->fragment_shader_path.empty()) {
    return false;
  }

  out_stage_job->buffer_bindings.reserve(phase_spec.used_algorithm_containers.size());
  for (const ::algomanager::bridge::AlgorithmPhaseContainerBinding& binding : phase_spec.used_algorithm_containers) {
    ::algorithm::AlgorithmContainer* container =
      ::algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK phase is missing container '" + binding.container_name + "'.")
          : ("VK optional container '" + binding.container_name +
              "' is not supported because runtime VK bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK phase container '" + binding.container_name + "' has no data.")
          : ("VK optional container '" + binding.container_name +
              "' has no data, and sparse VK bindings are not supported.");
      }
      return false;
    }

    runtimesys::RuntimeVkBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = binding.container_kind == "array";
    binding_view.draw_indirect = binding.container_kind == "draw_indirect";
    binding_view.required = binding.required;
    out_stage_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool TryBuildAlgorithmVkExecStageSubJob(
  const ::algomanager::bridge::AlgorithmObject& object,
  const ::algomanager::bridge::AlgorithmVkExecSpec& vk_exec_spec,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtimesys::RuntimeVkStageSubJob* out_stage_job,
  std::string* out_error_message) {
  if (!container_set || !out_stage_job) {
    if (out_error_message) {
      *out_error_message = "VK exec stage sub-job output pointer is null.";
    }
    return false;
  }
  if (vk_exec_spec.shader.vertex_shader_path.empty() || vk_exec_spec.shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "VK exec stage is missing shader paths.";
    }
    return false;
  }
  if (vk_exec_spec.used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "VK exec stage does not bind any containers.";
    }
    return false;
  }

  out_stage_job->debug_name = object.algorithm_profile.algorithm_name + "::" + vk_exec_spec.stage_name;
  out_stage_job->stage_name = vk_exec_spec.stage_name;
  out_stage_job->vertex_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    vk_exec_spec.shader.vertex_shader_path,
    out_error_message);
  if (out_stage_job->vertex_shader_path.empty()) {
    return false;
  }
  out_stage_job->fragment_shader_path = ResolveAlgorithmVkShaderPath(
    object,
    vk_exec_spec.shader.fragment_shader_path,
    out_error_message);
  if (out_stage_job->fragment_shader_path.empty()) {
    return false;
  }

  out_stage_job->buffer_bindings.reserve(vk_exec_spec.used_algorithm_containers.size());
  for (const ::algomanager::bridge::AlgorithmVkExecContainerBinding& binding : vk_exec_spec.used_algorithm_containers) {
    ::algorithm::AlgorithmContainer* container =
      ::algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK exec stage is missing container '" + binding.container_name + "'.")
          : ("VK exec optional container '" + binding.container_name +
              "' is not supported because runtime VK bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("VK exec stage container '" + binding.container_name + "' has no data.")
          : ("VK exec optional container '" + binding.container_name +
              "' has no data, and sparse VK bindings are not supported.");
      }
      return false;
    }

    runtimesys::RuntimeVkBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = binding.container_kind == "array";
    binding_view.draw_indirect = binding.container_kind == "draw_indirect";
    binding_view.required = binding.required;
    out_stage_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace execution_bridge_support
}  // namespace bridge



}  // namespace algomanager

