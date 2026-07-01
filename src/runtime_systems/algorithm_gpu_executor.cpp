#define RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD 1
#include "algorithm_support/algorithm_support.h"
#include "runtime_systems/gpu_job_system.h"
#undef RUNTIME_SYSTEMS_LAYER_INTERNAL_BUILD
#include "runtime_systems/runtime_gpu_context.h"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace runtime_systems {

namespace {

struct GpuBufferResource {
  VkBuffer buffer{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  VmaAllocationInfo allocation_info{};
  VkDeviceSize size_bytes{0u};
};

struct GpuBufferPairResource {
  GpuBufferResource input;
  GpuBufferResource output;
};

struct GpuExecutionState {
  std::vector<GpuBufferPairResource> buffers;
};

struct GpuOffscreenTarget {
  VkImage image{VK_NULL_HANDLE};
  VmaAllocation allocation{VK_NULL_HANDLE};
  VkImageView view{VK_NULL_HANDLE};
  VkRenderPass render_pass{VK_NULL_HANDLE};
  VkFramebuffer framebuffer{VK_NULL_HANDLE};
  VkExtent2D extent{1u, 1u};
  VkFormat format{VK_FORMAT_R8G8B8A8_UNORM};
};

struct GpuPipelineResource {
  VkDescriptorSetLayout descriptor_set_layout{VK_NULL_HANDLE};
  VkPipelineLayout pipeline_layout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
  uint32_t binding_count{0u};
  std::string shader_key;
};

struct RuntimeGpuViewportPushConstants {
  float width{0.0f};
  float height{0.0f};
};

std::string _MakeVkErrorMessage(const char* prefix, VkResult err) {
  return std::string(prefix) + " failed: VkResult=" + std::to_string(static_cast<int>(err));
}

void _CheckVkResult(VkResult err) {
  if (err != VK_SUCCESS) {
    throw std::runtime_error(_MakeVkErrorMessage("Vulkan call", err));
  }
}

VkDeviceSize _GrowBufferSize(VkDeviceSize current_size, VkDeviceSize required_size) {
  const VkDeviceSize minimum_size = std::max(required_size, static_cast<VkDeviceSize>(1u));
  if (current_size == 0u) {
    return minimum_size;
  }

  const VkDeviceSize half = std::max(current_size / 2u, static_cast<VkDeviceSize>(1u));
  if (current_size > std::numeric_limits<VkDeviceSize>::max() - half) {
    return minimum_size;
  }

  return std::max(minimum_size, current_size + half);
}

std::vector<std::byte> _ReadBinaryFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const std::streamoff end = file.tellg();
  if (end <= 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<size_t>(end));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    return {};
  }
  return bytes;
}

std::string _ResolveShaderBinaryPath(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const std::filesystem::path source_path(path);
  if (source_path.string().ends_with(".spv")) {
    return source_path.string();
  }
  return std::filesystem::path(source_path.string() + ".spv").string();
}

[[noreturn]] void _AbortGpuTick(std::string message);

std::string _StageShaderKey(
  const RuntimeGpuStageJob& job) {
  return job.shader_namespace + "|" + job.stage_name + "|" +
    job.vertex_shader_path + "|" +
    job.fragment_shader_path + "|" +
    std::to_string(job.buffer_bindings.size());
}

std::string _ExecutionStateKey(
  const RuntimeGpuStageJob& job,
  const std::string& shader_key) {
  return job.shader_namespace + "|" + shader_key + "|" +
    std::to_string(reinterpret_cast<std::uintptr_t>(job.execution_key));
}

[[noreturn]] void _ThrowGpuTickError(std::string message, std::string* out_error_message = nullptr) {
  if (out_error_message) {
    *out_error_message = message;
  }
  OutputDebugStringA((message + "\n").c_str());
  MessageBoxA(nullptr, message.c_str(), "GPU tick execution failed", MB_OK | MB_ICONERROR);
  assert(false && "GPU tick execution failed");
  throw std::runtime_error(std::move(message));
}

[[noreturn]] void _AbortGpuTick(std::string message) {
  OutputDebugStringA((message + "\n").c_str());
  MessageBoxA(nullptr, message.c_str(), "GPU tick execution failed", MB_OK | MB_ICONERROR);
  assert(false && "GPU tick execution failed");
  throw std::runtime_error(std::move(message));
}

bool _ResolveRuntimeGpuShaderPath(
  const ::agent::AlgorithmObject& object,
  const std::string& shader_path,
  std::string* out_resolved_path,
  std::string* out_error_message) {
  if (!out_resolved_path) {
    if (out_error_message) {
      *out_error_message = "Resolved GPU shader path output pointer is null.";
    }
    return false;
  }
  if (shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU shader path must not be empty.";
    }
    return false;
  }

  const std::filesystem::path path(shader_path);
  if (path.is_absolute()) {
    *out_resolved_path = path.string();
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  std::filesystem::path runtime_package_root(object.runtime_package_root_path);
  if (runtime_package_root.empty()) {
    ::algorithm::AlgorithmPackageLocation package_location{};
    std::string resolve_error_message;
    if (!TryResolveAlgorithmPackageLocation(
          object.algorithm_profile.algorithm_name,
          &package_location,
          &resolve_error_message)) {
      if (out_error_message) {
        *out_error_message = resolve_error_message.empty()
          ? ("Failed to resolve algorithm package location for '" + object.algorithm_profile.algorithm_name + "'.")
          : std::move(resolve_error_message);
      }
      return false;
    }
    runtime_package_root = package_location.runtime_package_root;
  }
  if (runtime_package_root.empty()) {
    if (out_error_message) {
      *out_error_message =
        "Algorithm runtime package root is empty for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return false;
  }

  *out_resolved_path = (runtime_package_root / path).lexically_normal().string();
  if (out_resolved_path->empty()) {
    if (out_error_message) {
      *out_error_message =
        "Failed to resolve GPU shader path for '" + object.algorithm_profile.algorithm_name + "'.";
    }
    return false;
  }
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryLoadRuntimeGpuExecSpec(
  const ::agent::AlgorithmObject& object,
  agent::AlgorithmGpuExecSpec* out_spec,
  bool* out_has_gpu_exec,
  std::string* out_error_message) {
  if (!out_spec || !out_has_gpu_exec) {
    if (out_error_message) {
      *out_error_message = "Runtime GPU exec spec output pointer is null.";
    }
    return false;
  }

  *out_spec = {};
  *out_has_gpu_exec = false;
  if (!object.gpu_executor) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  if (!object.gpu_executor->GetGpuExecSpec(out_spec)) {
    if (out_error_message) {
      *out_error_message = "GPU executor failed to provide its exec specification.";
    }
    return false;
  }
  if (out_spec->shader.vertex_shader_path.empty() ||
      out_spec->shader.fragment_shader_path.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage is missing shader paths.";
    }
    return false;
  }
  if (out_spec->used_algorithm_containers.empty()) {
    if (out_error_message) {
      *out_error_message = "GPU exec stage does not bind any containers.";
    }
    return false;
  }
  if (out_spec->stage_name.empty()) {
    out_spec->stage_name = "exec";
  }
  *out_has_gpu_exec = true;
  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

bool _TryBuildRuntimeGpuStageJob(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  runtime_systems::RuntimeGpuStageJob* out_job,
  bool* out_has_gpu_stage,
  std::string* out_error_message) {
  if (!container_set || !out_job || !out_has_gpu_stage) {
    if (out_error_message) {
      *out_error_message = "Runtime GPU stage job output pointer is null.";
    }
    return false;
  }

  *out_job = {};
  *out_has_gpu_stage = false;
  agent::AlgorithmGpuExecSpec gpu_exec_spec{};
  if (!_TryLoadRuntimeGpuExecSpec(
        object,
        &gpu_exec_spec,
        out_has_gpu_stage,
        out_error_message)) {
    return false;
  }
  if (!*out_has_gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }

  out_job->debug_name = object.algorithm_profile.algorithm_name + "::" + gpu_exec_spec.stage_name;
  out_job->shader_namespace = object.algorithm_profile.algorithm_name;
  out_job->stage_name = gpu_exec_spec.stage_name;
  out_job->execution_key = container_set;
  if (!_ResolveRuntimeGpuShaderPath(
        object,
        gpu_exec_spec.shader.vertex_shader_path,
        &out_job->vertex_shader_path,
        out_error_message)) {
    return false;
  }
  if (!_ResolveRuntimeGpuShaderPath(
        object,
        gpu_exec_spec.shader.fragment_shader_path,
        &out_job->fragment_shader_path,
        out_error_message)) {
    return false;
  }

  out_job->buffer_bindings.reserve(gpu_exec_spec.used_algorithm_containers.size());
  for (const agent::AlgorithmGpuExecContainerBinding& binding : gpu_exec_spec.used_algorithm_containers) {
    algorithm::AlgorithmContainer* container = algorithm::FindAlgorithmContainer(container_set, binding.container_name);
    if (!container) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU exec stage is missing container '" + binding.container_name + "'.")
          : ("GPU exec optional container '" + binding.container_name +
              "' is not supported because runtime GPU bindings must stay positional.");
      }
      return false;
    }
    if (container->element_stride == 0u || container->bytes.empty()) {
      if (out_error_message) {
        *out_error_message = binding.required
          ? ("GPU exec stage container '" + binding.container_name + "' has no data.")
          : ("GPU exec optional container '" + binding.container_name +
              "' has no data, and sparse GPU bindings are not supported.");
      }
      return false;
    }

    runtime_systems::RuntimeGpuBufferBindingView binding_view{};
    binding_view.binding_name = binding.container_name;
    binding_view.bytes = container->bytes.data();
    binding_view.size_bytes = container->bytes.size();
    binding_view.element_stride = container->element_stride;
    binding_view.array_like = container->storage_kind == algorithm::AlgorithmContainerStorageKind::Array;
    binding_view.required = binding.required;
    out_job->buffer_bindings.push_back(std::move(binding_view));
  }

  if (out_error_message) {
    out_error_message->clear();
  }
  return true;
}

}  // namespace

class GpuJobRuntimeSystem {
 public:
  static GpuJobRuntimeSystem& Instance() {
    static GpuJobRuntimeSystem instance{};
    return instance;
  }

  bool HasExecutableRuntimeGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
    agent::AlgorithmGpuExecSpec gpu_exec_spec{};
    bool has_gpu_exec = false;
    return _TryLoadRuntimeGpuExecSpec(object, &gpu_exec_spec, &has_gpu_exec, nullptr) && has_gpu_exec;
  }

  bool BuildRuntimeGpuStageJob(
    const ::agent::AlgorithmObject& object,
    ::algorithm::AlgorithmContainerSet* container_set,
    RuntimeGpuStageJob* out_job,
    bool* out_has_gpu_stage,
    std::string* out_error_message) {
    return _TryBuildRuntimeGpuStageJob(
      object,
      container_set,
      out_job,
      out_has_gpu_stage,
      out_error_message);
  }

  void Clear() {
    for (auto& [_, pipeline] : pipeline_cache_) {
      _DestroyPipeline(pipeline);
    }
    pipeline_cache_.clear();
    for (auto& [_, execution_state] : execution_state_cache_) {
      _DestroyExecutionState(execution_state);
    }
    execution_state_cache_.clear();
    _DestroyOffscreenTarget();
    _DestroyCommandResources();
    context_ = {};
  }

  bool ExecuteRuntimeGpuJob(
    const RuntimeGpuStageJob& job,
    std::string* out_error_message) {
    if (job.execution_key == nullptr) {
      _ThrowGpuTickError("Runtime GPU job execution key is null.", out_error_message);
    }
    if (job.vertex_shader_path.empty() || job.fragment_shader_path.empty()) {
      _ThrowGpuTickError("Runtime GPU job is missing shader paths.", out_error_message);
    }
    if (job.buffer_bindings.empty()) {
      _ThrowGpuTickError("Runtime GPU job does not bind any buffers.", out_error_message);
    }

    runtime_systems::RuntimeGpuExecutionContext execution_context =
      runtime_systems::RuntimeGpuContextRegistry::Instance().Snapshot();
    if (!execution_context.valid()) {
      _ThrowGpuTickError("GPU execution context is unavailable.", out_error_message);
    }

    try {
      _EnsureContext(execution_context);
      _UpdateOffscreenExtent(
        static_cast<uint32_t>(std::max(job.viewport_width, 1.0f)),
        static_cast<uint32_t>(std::max(job.viewport_height, 1.0f)));
      if (!_EnsureOffscreenTarget()) {
        _ThrowGpuTickError("Failed to create offscreen GPU target.", out_error_message);
      }

      const std::string shader_key = _StageShaderKey(job);
      GpuPipelineResource* pipeline =
        _GetOrCreatePipeline(shader_key, job);
      if (!pipeline) {
        _ThrowGpuTickError("Failed to create GPU pipeline.", out_error_message);
      }

      const std::string execution_state_key = _ExecutionStateKey(job, shader_key);
      auto state_it = execution_state_cache_.find(execution_state_key);
      if (state_it == execution_state_cache_.end()) {
        state_it = execution_state_cache_.emplace(execution_state_key, GpuExecutionState{}).first;
      }
      GpuExecutionState working_state = std::move(state_it->second);

      struct ExecutionCleanup {
        runtime_systems::RuntimeGpuExecutionContext context{};
        VkDescriptorSet descriptor_set{VK_NULL_HANDLE};
        ~ExecutionCleanup() {
          if (context.device != VK_NULL_HANDLE && descriptor_set != VK_NULL_HANDLE && context.descriptor_pool != VK_NULL_HANDLE) {
            vkFreeDescriptorSets(context.device, context.descriptor_pool, 1, &descriptor_set);
          }
        }
      } cleanup{execution_context, VK_NULL_HANDLE};

      uint32_t array_instance_count = 0u;
      bool have_array_instance_count = false;
      uint32_t fallback_instance_count = 0u;
      bool have_fallback_instance_count = false;
      size_t buffer_index = 0u;
      for (const RuntimeGpuBufferBindingView& binding : job.buffer_bindings) {
        if (binding.bytes == nullptr) {
          if (binding.required) {
            _ThrowGpuTickError(
              "Runtime GPU job is missing buffer '" + binding.binding_name + "'.",
              out_error_message);
          }
          continue;
        }
        if (binding.element_stride == 0u || binding.size_bytes == 0u) {
          if (binding.required) {
            _ThrowGpuTickError(
              "Runtime GPU buffer '" + binding.binding_name + "' has no data.",
              out_error_message);
          }
          continue;
        }

        if (buffer_index >= working_state.buffers.size()) {
          working_state.buffers.emplace_back();
        }
        GpuBufferPairResource& buffer_pair = working_state.buffers[buffer_index];
        const VkDeviceSize required_size = static_cast<VkDeviceSize>(binding.size_bytes);

        if (buffer_pair.input.buffer == VK_NULL_HANDLE || buffer_pair.input.size_bytes < required_size) {
          const VkDeviceSize grow_size = _GrowBufferSize(buffer_pair.input.size_bytes, required_size);
          _DestroyBuffer(buffer_pair.input);
          if (!_CreateStorageBuffer(grow_size, &buffer_pair.input)) {
            _ThrowGpuTickError(
              "Failed to create GPU buffer for '" + binding.binding_name + "'.",
              out_error_message);
          }
          std::memcpy(buffer_pair.input.allocation_info.pMappedData, binding.bytes, binding.size_bytes);
          _CheckVkResult(vmaFlushAllocation(
            execution_context.allocator,
            buffer_pair.input.allocation,
            0u,
            buffer_pair.input.size_bytes));
        }
        if (buffer_pair.output.buffer == VK_NULL_HANDLE || buffer_pair.output.size_bytes < required_size) {
          const VkDeviceSize grow_size = _GrowBufferSize(buffer_pair.output.size_bytes, required_size);
          _DestroyBuffer(buffer_pair.output);
          if (!_CreateStorageBuffer(grow_size, &buffer_pair.output)) {
            _ThrowGpuTickError(
              "Failed to create GPU output buffer for '" + binding.binding_name + "'.",
              out_error_message);
          }
          std::memcpy(buffer_pair.output.allocation_info.pMappedData, binding.bytes, binding.size_bytes);
          _CheckVkResult(vmaFlushAllocation(
            execution_context.allocator,
            buffer_pair.output.allocation,
            0u,
            buffer_pair.output.size_bytes));
        }

        // Bridge ingress updates host container bytes each stage tick.
        // Always upload the latest host view into this stage input buffer.
        std::memcpy(buffer_pair.input.allocation_info.pMappedData, binding.bytes, binding.size_bytes);
        _CheckVkResult(vmaFlushAllocation(
          execution_context.allocator,
          buffer_pair.input.allocation,
          0u,
          buffer_pair.input.size_bytes));

        const uint32_t buffer_instance_count =
          static_cast<uint32_t>(binding.size_bytes / binding.element_stride);

        if (binding.array_like) {
          if (!have_array_instance_count) {
            array_instance_count = buffer_instance_count;
            have_array_instance_count = true;
          } else {
            array_instance_count = std::min(array_instance_count, buffer_instance_count);
          }
        } else {
          if (!have_fallback_instance_count) {
            fallback_instance_count = buffer_instance_count;
            have_fallback_instance_count = true;
          } else {
            fallback_instance_count = std::min(fallback_instance_count, buffer_instance_count);
          }
        }
        ++buffer_index;
      }

      if (buffer_index == 0u) {
        _ThrowGpuTickError("GPU tick stage could not build any usable buffers.", out_error_message);
      }

      const uint32_t instance_count = have_array_instance_count
        ? array_instance_count
        : fallback_instance_count;
      if (instance_count == 0u) {
        _ThrowGpuTickError("GPU tick stage has no drawable instances.", out_error_message);
      }

      if (working_state.buffers.size() > buffer_index) {
        for (size_t i = buffer_index; i < working_state.buffers.size(); ++i) {
          _DestroyBuffer(working_state.buffers[i].input);
          _DestroyBuffer(working_state.buffers[i].output);
        }
        working_state.buffers.resize(buffer_index);
      }

      VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
      if (!_AllocateDescriptorSet(pipeline->descriptor_set_layout, &descriptor_set)) {
        _ThrowGpuTickError("Failed to allocate GPU descriptor set.", out_error_message);
      }
      cleanup.descriptor_set = descriptor_set;

      std::vector<VkDescriptorBufferInfo> buffer_infos;
      buffer_infos.reserve(working_state.buffers.size() * 2u);
      for (const GpuBufferPairResource& buffer_pair : working_state.buffers) {
        VkDescriptorBufferInfo input_info{};
        input_info.buffer = buffer_pair.input.buffer;
        input_info.offset = 0u;
        input_info.range = buffer_pair.input.size_bytes;
        buffer_infos.push_back(input_info);

        VkDescriptorBufferInfo output_info{};
        output_info.buffer = buffer_pair.output.buffer;
        output_info.offset = 0u;
        output_info.range = buffer_pair.output.size_bytes;
        buffer_infos.push_back(output_info);
      }

      std::vector<VkWriteDescriptorSet> descriptor_writes;
      descriptor_writes.reserve(buffer_infos.size());
      for (uint32_t i = 0; i < static_cast<uint32_t>(buffer_infos.size()); ++i) {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = descriptor_set;
        write.dstBinding = i;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.descriptorCount = 1;
        write.pBufferInfo = &buffer_infos[i];
        descriptor_writes.push_back(write);
      }
      if (!descriptor_writes.empty()) {
        vkUpdateDescriptorSets(
          execution_context.device,
          static_cast<uint32_t>(descriptor_writes.size()),
          descriptor_writes.data(),
          0,
          nullptr);
      }

      _RecordAndSubmit(*pipeline, descriptor_set, instance_count, execution_context);

      for (GpuBufferPairResource& buffer_pair : working_state.buffers) {
        std::swap(buffer_pair.input, buffer_pair.output);
      }

      state_it->second.buffers = std::move(working_state.buffers);

      return true;
    } catch (const std::exception& ex) {
      if (out_error_message) {
        *out_error_message = ex.what();
      }
      assert(false && "GPU tick execution failed");
      return false;
    } catch (...) {
      if (out_error_message) {
        *out_error_message = "GPU tick execution failed with an unknown error.";
      }
      assert(false && "GPU tick execution failed");
      return false;
    }
  }

  bool SynchronizeRuntimeGpuJob(
    const RuntimeGpuStageJob& job,
    std::string* out_error_message) {
    if (job.execution_key == nullptr) {
      _ThrowGpuTickError("Runtime GPU job synchronization key is null.", out_error_message);
    }

    const std::string shader_key = _StageShaderKey(job);
    const std::string execution_state_key = _ExecutionStateKey(job, shader_key);
    auto state_it = execution_state_cache_.find(execution_state_key);
    if (state_it == execution_state_cache_.end() || state_it->second.buffers.empty()) {
      _ThrowGpuTickError(
        "GPU tick synchronization failed because no cached GPU state exists.",
        out_error_message);
    }

    uint32_t synced_bindings = 0u;
    for (const RuntimeGpuBufferBindingView& binding : job.buffer_bindings) {
      if (binding.bytes == nullptr) {
        if (binding.required) {
          _ThrowGpuTickError(
            "Runtime GPU synchronization is missing buffer '" + binding.binding_name + "'.",
            out_error_message);
        }
        continue;
      }

      if (synced_bindings >= state_it->second.buffers.size()) {
        _ThrowGpuTickError(
          "GPU tick synchronization ran out of cached GPU buffers.",
          out_error_message);
      }

      GpuBufferPairResource& buffer_pair = state_it->second.buffers[synced_bindings];
      if (buffer_pair.input.buffer == VK_NULL_HANDLE || buffer_pair.input.allocation_info.pMappedData == nullptr) {
        _ThrowGpuTickError(
          "GPU tick synchronization encountered an invalid GPU input buffer.",
          out_error_message);
      }
      _CheckVkResult(vmaInvalidateAllocation(
        context_.allocator,
        buffer_pair.input.allocation,
        0u,
        buffer_pair.input.size_bytes));
      const size_t copy_size = std::min(binding.size_bytes, static_cast<size_t>(buffer_pair.input.size_bytes));
      std::memcpy(
        binding.bytes,
        buffer_pair.input.allocation_info.pMappedData,
        copy_size);
      if (binding.size_bytes > copy_size) {
        std::memset(
          binding.bytes + copy_size,
          0,
          binding.size_bytes - copy_size);
      }
      ++synced_bindings;
    }

    if (synced_bindings == 0u) {
      _ThrowGpuTickError(
        "GPU tick synchronization found no usable containers.",
        out_error_message);
    }
    if (synced_bindings != state_it->second.buffers.size()) {
      _ThrowGpuTickError(
        "GPU tick synchronization ended with an unexpected cached buffer count.",
        out_error_message);
    }

    return true;
  }

 private:
  struct CommandResources {
    VkCommandPool command_pool{VK_NULL_HANDLE};
    VkCommandBuffer command_buffer{VK_NULL_HANDLE};
  };

  void _DestroyBuffer(GpuBufferResource& buffer) {
    if (buffer.buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(context_.allocator, buffer.buffer, buffer.allocation);
      buffer.buffer = VK_NULL_HANDLE;
      buffer.allocation = VK_NULL_HANDLE;
      buffer.allocation_info = {};
      buffer.size_bytes = 0u;
    }
  }

  void _DestroyExecutionState(GpuExecutionState& execution_state) {
    for (GpuBufferPairResource& buffer_pair : execution_state.buffers) {
      _DestroyBuffer(buffer_pair.input);
      _DestroyBuffer(buffer_pair.output);
    }
    execution_state.buffers.clear();
  }

  void _DestroyPipeline(GpuPipelineResource& pipeline) {
    if (context_.device != VK_NULL_HANDLE) {
      if (pipeline.pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(context_.device, pipeline.pipeline, nullptr);
        pipeline.pipeline = VK_NULL_HANDLE;
      }
      if (pipeline.pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(context_.device, pipeline.pipeline_layout, nullptr);
        pipeline.pipeline_layout = VK_NULL_HANDLE;
      }
      if (pipeline.descriptor_set_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(context_.device, pipeline.descriptor_set_layout, nullptr);
        pipeline.descriptor_set_layout = VK_NULL_HANDLE;
      }
    }
  }

  void _DestroyOffscreenTarget() {
    if (context_.device != VK_NULL_HANDLE) {
      if (offscreen_target_.framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(context_.device, offscreen_target_.framebuffer, nullptr);
        offscreen_target_.framebuffer = VK_NULL_HANDLE;
      }
      if (offscreen_target_.render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(context_.device, offscreen_target_.render_pass, nullptr);
        offscreen_target_.render_pass = VK_NULL_HANDLE;
      }
      if (offscreen_target_.view != VK_NULL_HANDLE) {
        vkDestroyImageView(context_.device, offscreen_target_.view, nullptr);
        offscreen_target_.view = VK_NULL_HANDLE;
      }
    }
    if (offscreen_target_.image != VK_NULL_HANDLE) {
      vmaDestroyImage(context_.allocator, offscreen_target_.image, offscreen_target_.allocation);
      offscreen_target_.image = VK_NULL_HANDLE;
      offscreen_target_.allocation = VK_NULL_HANDLE;
    }
  }

  void _DestroyCommandResources() {
    if (context_.device != VK_NULL_HANDLE && command_resources_.command_pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(context_.device, command_resources_.command_pool, nullptr);
      command_resources_.command_pool = VK_NULL_HANDLE;
      command_resources_.command_buffer = VK_NULL_HANDLE;
    }
  }

  void _EnsureContext(runtime_systems::RuntimeGpuExecutionContext context) {
    const bool needs_reset =
      context_.device != context.device ||
      context_.allocator != context.allocator ||
      context_.queue != context.queue ||
      context_.queue_family != context.queue_family ||
      context_.descriptor_pool != context.descriptor_pool;
    if (needs_reset) {
      Clear();
      context_ = context;
      _CreateCommandResources();
    } else if (!context_.valid()) {
      context_ = context;
      _CreateCommandResources();
    }
  }

  void _CreateCommandResources() {
    if (!context_.valid() || context_.device == VK_NULL_HANDLE) {
      return;
    }

    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context_.queue_family;
    _CheckVkResult(vkCreateCommandPool(context_.device, &pool_info, nullptr, &command_resources_.command_pool));

    VkCommandBufferAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    alloc_info.commandPool = command_resources_.command_pool;
    alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc_info.commandBufferCount = 1;
    _CheckVkResult(vkAllocateCommandBuffers(context_.device, &alloc_info, &command_resources_.command_buffer));
  }

  void _UpdateOffscreenExtent(uint32_t width, uint32_t height) {
    width = std::max<uint32_t>(width, 1u);
    height = std::max<uint32_t>(height, 1u);
    if (offscreen_target_.extent.width == width && offscreen_target_.extent.height == height) {
      return;
    }
    _DestroyOffscreenTarget();
    offscreen_target_.extent = VkExtent2D{width, height};
  }

  bool _EnsureOffscreenTarget() {
    if (!context_.valid() || offscreen_target_.render_pass != VK_NULL_HANDLE) {
      return offscreen_target_.render_pass != VK_NULL_HANDLE;
    }

    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = offscreen_target_.format;
    image_info.extent = {offscreen_target_.extent.width, offscreen_target_.extent.height, 1u};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    _CheckVkResult(vmaCreateImage(
      context_.allocator,
      &image_info,
      &allocation_info,
      &offscreen_target_.image,
      &offscreen_target_.allocation,
      nullptr));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = offscreen_target_.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = offscreen_target_.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    _CheckVkResult(vkCreateImageView(context_.device, &view_info, nullptr, &offscreen_target_.view));

    VkAttachmentDescription attachment{};
    attachment.format = offscreen_target_.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_attachment{};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    _CheckVkResult(vkCreateRenderPass(context_.device, &render_pass_info, nullptr, &offscreen_target_.render_pass));

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = offscreen_target_.render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &offscreen_target_.view;
    framebuffer_info.width = offscreen_target_.extent.width;
    framebuffer_info.height = offscreen_target_.extent.height;
    framebuffer_info.layers = 1;
    _CheckVkResult(vkCreateFramebuffer(context_.device, &framebuffer_info, nullptr, &offscreen_target_.framebuffer));
    return true;
  }

  bool _CreateStorageBuffer(VkDeviceSize size, GpuBufferResource* out_buffer) const {
    if (!out_buffer || !context_.valid()) {
      _AbortGpuTick("GPU tick storage buffer creation failed: invalid output buffer or execution context.");
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = std::max<VkDeviceSize>(size, 1u);
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags =
      VMA_ALLOCATION_CREATE_MAPPED_BIT |
      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

    _CheckVkResult(vmaCreateBuffer(
      context_.allocator,
      &buffer_info,
      &allocation_info,
      &out_buffer->buffer,
      &out_buffer->allocation,
      &out_buffer->allocation_info));
    out_buffer->size_bytes = buffer_info.size;
    return true;
  }

  bool _AllocateDescriptorSet(VkDescriptorSetLayout descriptor_set_layout, VkDescriptorSet* out_descriptor_set) const {
    if (!out_descriptor_set || context_.descriptor_pool == VK_NULL_HANDLE) {
      _AbortGpuTick("GPU tick descriptor set allocation failed: invalid output pointer or descriptor pool.");
    }

    VkDescriptorSetAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    alloc_info.descriptorPool = context_.descriptor_pool;
    alloc_info.descriptorSetCount = 1;
    alloc_info.pSetLayouts = &descriptor_set_layout;
    const VkResult result = vkAllocateDescriptorSets(context_.device, &alloc_info, out_descriptor_set);
    if (result != VK_SUCCESS) {
      _AbortGpuTick(_MakeVkErrorMessage("vkAllocateDescriptorSets", result));
    }
    return true;
  }

  GpuPipelineResource* _GetOrCreatePipeline(
    const std::string& shader_key,
    const RuntimeGpuStageJob& job) {
    const auto found = pipeline_cache_.find(shader_key);
    if (found != pipeline_cache_.end()) {
      return &found->second;
    }

    if (context_.device == VK_NULL_HANDLE || offscreen_target_.render_pass == VK_NULL_HANDLE) {
      _AbortGpuTick("GPU tick pipeline creation failed: Vulkan device or offscreen render pass is unavailable.");
    }

    GpuPipelineResource pipeline{};
    pipeline.shader_key = shader_key;
    pipeline.binding_count = static_cast<uint32_t>(job.buffer_bindings.size() * 2u);

    const std::string vertex_path =
      _ResolveShaderBinaryPath(job.vertex_shader_path);
    const std::string fragment_path =
      _ResolveShaderBinaryPath(job.fragment_shader_path);
    const std::vector<std::byte> vertex_bytes = _ReadBinaryFile(vertex_path);
    const std::vector<std::byte> fragment_bytes = _ReadBinaryFile(fragment_path);
    if (vertex_bytes.empty() || fragment_bytes.empty()) {
      _AbortGpuTick(
        "GPU tick pipeline creation failed: shader binaries could not be loaded for '" +
        job.debug_name + "'.");
    }

    VkShaderModule vertex_shader_module = VK_NULL_HANDLE;
    VkShaderModule fragment_shader_module = VK_NULL_HANDLE;
    struct ShaderCleanup {
      VkDevice device{VK_NULL_HANDLE};
      VkShaderModule* module{nullptr};
      ~ShaderCleanup() {
        if (device != VK_NULL_HANDLE && module && *module != VK_NULL_HANDLE) {
          vkDestroyShaderModule(device, *module, nullptr);
        }
      }
    } vertex_cleanup{context_.device, &vertex_shader_module}, fragment_cleanup{context_.device, &fragment_shader_module};

    VkShaderModuleCreateInfo vertex_module_info{};
    vertex_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_module_info.codeSize = vertex_bytes.size();
    vertex_module_info.pCode = reinterpret_cast<const uint32_t*>(vertex_bytes.data());

    VkShaderModuleCreateInfo fragment_module_info{};
    fragment_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_module_info.codeSize = fragment_bytes.size();
    fragment_module_info.pCode = reinterpret_cast<const uint32_t*>(fragment_bytes.data());

    _CheckVkResult(vkCreateShaderModule(context_.device, &vertex_module_info, nullptr, &vertex_shader_module));
    _CheckVkResult(vkCreateShaderModule(context_.device, &fragment_module_info, nullptr, &fragment_shader_module));

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(job.buffer_bindings.size());
    for (uint32_t i = 0; i < pipeline.binding_count; ++i) {
      VkDescriptorSetLayoutBinding binding{};
      binding.binding = i;
      binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount = 1;
      binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();
    _CheckVkResult(vkCreateDescriptorSetLayout(context_.device, &layout_info, nullptr, &pipeline.descriptor_set_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &pipeline.descriptor_set_layout;
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0u;
    push_constant_range.size = sizeof(RuntimeGpuViewportPushConstants);
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;
    _CheckVkResult(vkCreatePipelineLayout(context_.device, &pipeline_layout_info, nullptr, &pipeline.pipeline_layout));

    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vertex_shader_module;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fragment_shader_module;
    shader_stages[1].pName = "main";

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_blend_attachment;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    const std::vector<VkDynamicState> dynamic_states = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline.pipeline_layout;
    pipeline_info.renderPass = offscreen_target_.render_pass;
    pipeline_info.subpass = 0;

    _CheckVkResult(vkCreateGraphicsPipelines(
      context_.device,
      VK_NULL_HANDLE,
      1,
      &pipeline_info,
      nullptr,
      &pipeline.pipeline));

    auto [inserted, inserted_ok] = pipeline_cache_.emplace(shader_key, std::move(pipeline));
    (void)inserted_ok;
    return &inserted->second;
  }

  void _RecordAndSubmit(
    const GpuPipelineResource& pipeline,
    VkDescriptorSet descriptor_set,
    uint32_t instance_count,
    const runtime_systems::RuntimeGpuExecutionContext& execution_context) {
    _CheckVkResult(vkResetCommandPool(execution_context.device, command_resources_.command_pool, 0));

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    _CheckVkResult(vkBeginCommandBuffer(command_resources_.command_buffer, &begin_info));

    VkClearValue clear_value{};
    clear_value.color.float32[0] = 0.0f;
    clear_value.color.float32[1] = 0.0f;
    clear_value.color.float32[2] = 0.0f;
    clear_value.color.float32[3] = 0.0f;

    VkRenderPassBeginInfo render_pass_begin_info{};
    render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    render_pass_begin_info.renderPass = offscreen_target_.render_pass;
    render_pass_begin_info.framebuffer = offscreen_target_.framebuffer;
    render_pass_begin_info.renderArea.extent = offscreen_target_.extent;
    render_pass_begin_info.clearValueCount = 1;
    render_pass_begin_info.pClearValues = &clear_value;
    vkCmdBeginRenderPass(command_resources_.command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(offscreen_target_.extent.height);
    viewport.width = static_cast<float>(offscreen_target_.extent.width);
    viewport.height = -static_cast<float>(offscreen_target_.extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = offscreen_target_.extent;

    vkCmdBindPipeline(command_resources_.command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.pipeline);
    vkCmdBindDescriptorSets(
      command_resources_.command_buffer,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      pipeline.pipeline_layout,
      0,
      1,
      &descriptor_set,
      0,
      nullptr);
    const RuntimeGpuViewportPushConstants push_constants{
      static_cast<float>(offscreen_target_.extent.width),
      static_cast<float>(offscreen_target_.extent.height),
    };
    vkCmdPushConstants(
      command_resources_.command_buffer,
      pipeline.pipeline_layout,
      VK_SHADER_STAGE_VERTEX_BIT,
      0,
      sizeof(push_constants),
      &push_constants);
    vkCmdSetViewport(command_resources_.command_buffer, 0, 1, &viewport);
    vkCmdSetScissor(command_resources_.command_buffer, 0, 1, &scissor);
    // GPU tick uses the same quad-friendly triangle-strip path as the preview
    // renderer so the vertex stage definitely runs on a real primitive.
    // The shader only writes back on vertex 0 of each instance.
    vkCmdDraw(command_resources_.command_buffer, 4, instance_count, 0, 0);

    vkCmdEndRenderPass(command_resources_.command_buffer);
    _CheckVkResult(vkEndCommandBuffer(command_resources_.command_buffer));

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info{};
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 0;
    submit_info.pWaitSemaphores = nullptr;
    submit_info.pWaitDstStageMask = &wait_stage;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &command_resources_.command_buffer;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;

    _CheckVkResult(vkQueueSubmit(execution_context.queue, 1, &submit_info, VK_NULL_HANDLE));
    _CheckVkResult(vkQueueWaitIdle(execution_context.queue));
  }

  runtime_systems::RuntimeGpuExecutionContext context_{};
  CommandResources command_resources_{};
  GpuOffscreenTarget offscreen_target_{};
  std::unordered_map<std::string, GpuPipelineResource> pipeline_cache_{};
  std::unordered_map<std::string, GpuExecutionState> execution_state_cache_{};
};

void ClearRuntimeGpuJobCaches() {
  GpuJobRuntimeSystem::Instance().Clear();
}

bool ExecuteRuntimeGpuJob(
  const RuntimeGpuStageJob& job,
  std::string* out_error_message) {
  return GpuJobRuntimeSystem::Instance().ExecuteRuntimeGpuJob(
    job,
    out_error_message);
}

bool SynchronizeRuntimeGpuJob(
  const RuntimeGpuStageJob& job,
  std::string* out_error_message) {
  return GpuJobRuntimeSystem::Instance().SynchronizeRuntimeGpuJob(
    job,
    out_error_message);
}

bool HasExecutableRuntimeGpuAlgorithmStage(const ::agent::AlgorithmObject& object) {
  return GpuJobRuntimeSystem::Instance().HasExecutableRuntimeGpuAlgorithmStage(object);
}

bool ExecuteRuntimeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  const ::agent::AgentTickContext& context,
  std::string* out_error_message) {
  RuntimeGpuStageJob job{};
  bool has_gpu_stage = false;
  if (!GpuJobRuntimeSystem::Instance().BuildRuntimeGpuStageJob(
        object,
        container_set,
        &job,
        &has_gpu_stage,
        out_error_message)) {
    return false;
  }
  if (!has_gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  job.viewport_width = std::max(context.render_preview_extent.x, 1.0f);
  job.viewport_height = std::max(context.render_preview_extent.y, 1.0f);
  return GpuJobRuntimeSystem::Instance().ExecuteRuntimeGpuJob(job, out_error_message);
}

bool SynchronizeRuntimeGpuAlgorithmObject(
  const ::agent::AlgorithmObject& object,
  ::algorithm::AlgorithmContainerSet* container_set,
  std::string* out_error_message) {
  RuntimeGpuStageJob job{};
  bool has_gpu_stage = false;
  if (!GpuJobRuntimeSystem::Instance().BuildRuntimeGpuStageJob(
        object,
        container_set,
        &job,
        &has_gpu_stage,
        out_error_message)) {
    return false;
  }
  if (!has_gpu_stage) {
    if (out_error_message) {
      out_error_message->clear();
    }
    return true;
  }
  return GpuJobRuntimeSystem::Instance().SynchronizeRuntimeGpuJob(job, out_error_message);
}

}  // namespace runtime_systems
