#pragma once

#include "algorithm_types.h"
#include "codec/codec_manager.h"

#include <memory>
#include <string>
#include <vector>

namespace algorithm {

struct AlgorithmPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
};

class IAlgorithmPackageCodec {
 public:
  virtual ~IAlgorithmPackageCodec() = default;

  // Complex algorithms can override conversion/reflection with custom logic.
  virtual bool BuildComplianceDescriptor(
    const codec::VolumeDescriptor& volume,
    AlgorithmComplianceDescriptor* out_descriptor) const = 0;
};

class ISimpleAlgorithmPackageCodec : public IAlgorithmPackageCodec {
 public:
  ~ISimpleAlgorithmPackageCodec() override = default;
};

class IComplexAlgorithmPackageCodec : public IAlgorithmPackageCodec {
 public:
  ~IComplexAlgorithmPackageCodec() override = default;
  virtual void CollectDebugState(AlgorithmPackageDebugState* debug_state) const = 0;
};

struct AlgorithmPackageHandle {
  std::string package_name;
  std::shared_ptr<IAlgorithmPackageCodec> codec_hook;
};

}  // namespace algorithm

using algorithm::AlgorithmPackageDebugState;
using algorithm::AlgorithmPackageHandle;
using algorithm::IAlgorithmPackageCodec;
using algorithm::IComplexAlgorithmPackageCodec;
using algorithm::ISimpleAlgorithmPackageCodec;
