#pragma once

#include "common_data/interaction/interaction_signals.h"

#include <cstdint>
#include <string>
#include <vector>

namespace algorithm {

struct AlgorithmBufferRequirement {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
};

struct AlgorithmDataFormat {
  std::string name;
  uint32_t element_count{};
  uint32_t element_stride{};
};

struct AlgorithmContainerAlias {
  std::string package_name;
  std::string source_name;
  std::string alias_name;
};

struct AlgorithmDataContract {
  std::vector<AlgorithmBufferRequirement> arrays_to_allocate;
  std::vector<AlgorithmBufferRequirement> temporary_registers_to_allocate;
  std::vector<AlgorithmBufferRequirement> temporary_caches_to_allocate;
  std::vector<AlgorithmDataFormat> filled_data_formats;
  std::vector<AlgorithmDataFormat> algorithm_required_formats;
  std::vector<AlgorithmContainerAlias> container_aliases;
};

struct AlgorithmComplianceDescriptor {
  std::string algorithm_name;
  bool cpu_available{true};
  bool gpu_available{false};
  float motion_radius{0.0f};
  AlgorithmDataContract data_contract{};
};

using AlgorithmContainerDescriptor = AlgorithmComplianceDescriptor;

inline const AlgorithmContainerAlias* FindAlgorithmContainerAlias(
  const AlgorithmDataContract& data_contract,
  const std::string& package_name,
  const std::string& source_name) {
  for (const auto& alias : data_contract.container_aliases) {
    if ((alias.package_name.empty() || alias.package_name == package_name) && alias.source_name == source_name) {
      return &alias;
    }
  }
  return nullptr;
}

inline std::string ResolveAlgorithmContainerName(
  const AlgorithmDataContract& data_contract,
  const std::string& package_name,
  const std::string& source_name) {
  const AlgorithmContainerAlias* alias = FindAlgorithmContainerAlias(data_contract, package_name, source_name);
  return alias ? alias->alias_name : source_name;
}

inline std::string ResolveAlgorithmContainerName(
  const AlgorithmComplianceDescriptor& compliance_descriptor,
  const std::string& package_name,
  const std::string& source_name) {
  return ResolveAlgorithmContainerName(compliance_descriptor.data_contract, package_name, source_name);
}

}  // namespace algorithm

using algorithm::AlgorithmBufferRequirement;
using algorithm::AlgorithmComplianceDescriptor;
using algorithm::AlgorithmContainerDescriptor;
using algorithm::AlgorithmDataContract;
using algorithm::AlgorithmDataFormat;
using algorithm::AlgorithmContainerAlias;
