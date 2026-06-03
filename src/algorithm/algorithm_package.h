#pragma once

#include "algorithm_types.h"
#include "codec/codec_manager.h"
#include "common_data/interaction/interaction_signals.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace algorithm {

struct AlgorithmReadableReflection {
  std::string algorithm_name;
  std::vector<std::pair<std::string, std::string>> fields;
  bool valid{false};
};

struct AlgorithmDescriptorShapeReflection {
  std::string algorithm_name;
  AlgorithmContainerDescriptor descriptor_shape{};
  bool valid{false};
};

struct AlgorithmDecompositionReflection {
  std::string algorithm_name;
  std::vector<std::string> required_resources;
  bool valid{false};
};

struct AlgorithmPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
};

struct AlgorithmInterventionPackageDebugState {
  std::vector<codec::AdvancedAlgorithmDebugSignal> signals;
  AlgorithmToAgentSignal algorithm_to_agent_signal{};
};

class IAlgorithmPackageCodec {
 public:
  virtual ~IAlgorithmPackageCodec() = default;

  // Complex algorithms can override conversion/reflection with custom logic.
  virtual bool BuildContainerDescriptor(
    const codec::VolumeDescriptor& volume,
    AlgorithmContainerDescriptor* out_descriptor) const = 0;

  virtual bool BuildMeshCoderOutput(const Mesh& mesh, MeshCoderOutput* out_output) const {
    (void)mesh;
    (void)out_output;
    return false;
  }

  virtual bool ReflectMeshCommon(const Mesh& mesh, MeshCommonReflection* out_reflection) const {
    (void)mesh;
    (void)out_reflection;
    return false;
  }

  // Reflect the container into a human-friendly summary for upper layers.
  virtual bool ReflectReadableParameters(
    const AlgorithmContainerDescriptor& container_descriptor,
    AlgorithmReadableReflection* out_reflection) const {
    (void)container_descriptor;
    (void)out_reflection;
    return false;
  }

  virtual bool BuildVolumeDescriptor(
    const Mesh& mesh,
    float mass,
    Vec3 driving_dir,
    VolumeDescriptor* out_volume) const {
    (void)mesh;
    (void)mass;
    (void)driving_dir;
    (void)out_volume;
    return false;
  }
};

class IAlgorithmPackageDecomposer {
 public:
  virtual ~IAlgorithmPackageDecomposer() = default;

  virtual bool ReflectDecomposition(
    const AlgorithmContainerDescriptor& container_descriptor,
    AlgorithmDecompositionReflection* out_reflection) const {
    (void)container_descriptor;
    (void)out_reflection;
    return false;
  }
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

class IAlgorithmInterventionPackageCodec {
 public:
  virtual ~IAlgorithmInterventionPackageCodec() = default;

  virtual bool BuildInterventionPacket(
    const InteractionInterventionRequest& request,
    IoBufferPacket* packet) const = 0;
  virtual bool DecodeInterventionPacket(
    const IoBufferPacket& packet,
    InteractionInterventionRequest* request) const = 0;
};

class IAlgorithmInterventionPackageAgent {
 public:
  virtual ~IAlgorithmInterventionPackageAgent() = default;

  virtual bool NeedsIntervention(const AgentToAlgorithmSignal& signal) const = 0;
  virtual bool ShouldPause(const AgentToAlgorithmSignal& signal) const = 0;
};

class IAlgorithmInterventionPackageAlgorithm {
 public:
  virtual ~IAlgorithmInterventionPackageAlgorithm() = default;

  virtual bool SupportsIntervention() const = 0;
};

struct AlgorithmPackageHandle {
  std::string package_name;
  std::shared_ptr<IAlgorithmPackageCodec> codec_hook;
};

struct AlgorithmInterventionPackageHandle {
  std::string package_name;
  std::shared_ptr<IAlgorithmInterventionPackageCodec> codec_hook;
  std::shared_ptr<IAlgorithmInterventionPackageAgent> agent_hook;
  std::shared_ptr<IAlgorithmInterventionPackageAlgorithm> algorithm_hook;
};

}  // namespace algorithm

using algorithm::AlgorithmPackageDebugState;
using algorithm::AlgorithmPackageHandle;
using algorithm::AlgorithmDescriptorShapeReflection;
using algorithm::AlgorithmDecompositionReflection;
using algorithm::AlgorithmInterventionPackageDebugState;
using algorithm::AlgorithmInterventionPackageHandle;
using algorithm::AlgorithmReadableReflection;
using algorithm::IAlgorithmInterventionPackageAgent;
using algorithm::IAlgorithmInterventionPackageAlgorithm;
using algorithm::IAlgorithmInterventionPackageCodec;
using algorithm::IAlgorithmPackageCodec;
using algorithm::IAlgorithmPackageDecomposer;
using algorithm::IComplexAlgorithmPackageCodec;
using algorithm::ISimpleAlgorithmPackageCodec;
