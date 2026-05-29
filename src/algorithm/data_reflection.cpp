#include "data_reflection.h"

#include <algorithm>
#include <cstring>

namespace {

void _AllocateBlocks(const std::vector<ReflectionMemoryRequest>& requests, std::vector<ReflectionMemoryBlock>* blocks) {
  if (!blocks) return;
  blocks->clear();
  blocks->reserve(requests.size());
  for (const ReflectionMemoryRequest& request : requests) {
    ReflectionMemoryBlock block{};
    block.name = request.name;
    block.element_count = request.element_count;
    block.element_stride = request.element_stride;
    block.bytes.resize(static_cast<size_t>(request.element_count) * static_cast<size_t>(request.element_stride));
    blocks->push_back(std::move(block));
  }
}

void _BlindCopyIntoBlocks(const CreateDataReflectionInfo& reflection_info, const std::vector<const void*>& real_data_addresses, std::vector<ReflectionMemoryBlock>* blocks) {
  if (!blocks) return;

  const size_t copy_count = std::min({blocks->size(), real_data_addresses.size(), reflection_info.filled_data_formats.size()});
  for (size_t i = 0; i < copy_count; ++i) {
    if (!real_data_addresses[i]) continue;
    const ReflectionDataFormat& format = reflection_info.filled_data_formats[i];
    ReflectionMemoryBlock& block = (*blocks)[i];
    const size_t copy_bytes = std::min(
      block.bytes.size(),
      static_cast<size_t>(format.element_count) * static_cast<size_t>(format.element_stride));
    if (copy_bytes == 0) continue;
    std::memcpy(block.bytes.data(), real_data_addresses[i], copy_bytes);
  }
}

void _FillBlocksWithFloatValue(float value, std::vector<ReflectionMemoryBlock>* blocks) {
  if (!blocks) return;
  for (ReflectionMemoryBlock& block : *blocks) {
    if (block.element_stride != sizeof(float) || block.bytes.empty()) {
      continue;
    }
    const size_t float_count = block.bytes.size() / sizeof(float);
    for (size_t i = 0; i < float_count; ++i) {
      std::memcpy(block.bytes.data() + i * sizeof(float), &value, sizeof(float));
    }
  }
}

void _CpuJobReflectionCallback(
  const CreateDataReflectionInfo& reflection_info,
  const std::vector<const void*>& real_data_addresses,
  DataReflectionCommit* reflection_commit) {
  if (!reflection_commit) return;

  _AllocateBlocks(reflection_info.arrays_to_allocate, &reflection_commit->arrays);
  _AllocateBlocks(reflection_info.temporary_registers_to_allocate, &reflection_commit->temporary_registers);
  _AllocateBlocks(reflection_info.temporary_caches_to_allocate, &reflection_commit->temporary_caches);

  _BlindCopyIntoBlocks(reflection_info, real_data_addresses, &reflection_commit->arrays);
  reflection_commit->filled_data_formats = reflection_info.filled_data_formats;
  reflection_commit->algorithm_required_formats = reflection_info.algorithm_required_formats;
  reflection_commit->valid = true;
}

void _GpuShaderReflectionCallback(
  const CreateDataReflectionInfo& reflection_info,
  const std::vector<const void*>& real_data_addresses,
  DataReflectionCommit* reflection_commit) {
  if (!reflection_commit) return;

  _AllocateBlocks(reflection_info.arrays_to_allocate, &reflection_commit->arrays);
  _AllocateBlocks(reflection_info.temporary_registers_to_allocate, &reflection_commit->temporary_registers);
  _AllocateBlocks(reflection_info.temporary_caches_to_allocate, &reflection_commit->temporary_caches);

  _BlindCopyIntoBlocks(reflection_info, real_data_addresses, &reflection_commit->arrays);
  _BlindCopyIntoBlocks(reflection_info, real_data_addresses, &reflection_commit->temporary_registers);
  if (real_data_addresses.empty()) {
    _FillBlocksWithFloatValue(1.0f, &reflection_commit->arrays);
  }
  reflection_commit->filled_data_formats = reflection_info.filled_data_formats;
  reflection_commit->algorithm_required_formats = reflection_info.algorithm_required_formats;
  reflection_commit->valid = true;
}

}  // namespace

CreateDataReflectionInfo CreateCpuJobDataReflectionInfo(
  std::vector<ReflectionMemoryRequest> arrays_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_registers_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_caches_to_allocate,
  std::vector<ReflectionDataFormat> filled_data_formats,
  std::vector<ReflectionDataFormat> algorithm_required_formats) {
  CreateDataReflectionInfo info{};
  info.arrays_to_allocate = std::move(arrays_to_allocate);
  info.temporary_registers_to_allocate = std::move(temporary_registers_to_allocate);
  info.temporary_caches_to_allocate = std::move(temporary_caches_to_allocate);
  info.filled_data_formats = std::move(filled_data_formats);
  info.algorithm_required_formats = std::move(algorithm_required_formats);
  info.reflection_callback = _CpuJobReflectionCallback;
  return info;
}

CreateDataReflectionInfo CreateGpuShaderDataReflectionInfo(
  std::vector<ReflectionMemoryRequest> arrays_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_registers_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_caches_to_allocate,
  std::vector<ReflectionDataFormat> filled_data_formats,
  std::vector<ReflectionDataFormat> algorithm_required_formats) {
  CreateDataReflectionInfo info{};
  info.arrays_to_allocate = std::move(arrays_to_allocate);
  info.temporary_registers_to_allocate = std::move(temporary_registers_to_allocate);
  info.temporary_caches_to_allocate = std::move(temporary_caches_to_allocate);
  info.filled_data_formats = std::move(filled_data_formats);
  info.algorithm_required_formats = std::move(algorithm_required_formats);
  info.reflection_callback = _GpuShaderReflectionCallback;
  return info;
}
