#pragma once

#include "algorithm_types.h"

#include <vector>

CreateDataReflectionInfo CreateCpuJobDataReflectionInfo(
  std::vector<ReflectionMemoryRequest> arrays_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_registers_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_caches_to_allocate,
  std::vector<ReflectionDataFormat> filled_data_formats,
  std::vector<ReflectionDataFormat> algorithm_required_formats);

CreateDataReflectionInfo CreateGpuShaderDataReflectionInfo(
  std::vector<ReflectionMemoryRequest> arrays_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_registers_to_allocate,
  std::vector<ReflectionMemoryRequest> temporary_caches_to_allocate,
  std::vector<ReflectionDataFormat> filled_data_formats,
  std::vector<ReflectionDataFormat> algorithm_required_formats);
