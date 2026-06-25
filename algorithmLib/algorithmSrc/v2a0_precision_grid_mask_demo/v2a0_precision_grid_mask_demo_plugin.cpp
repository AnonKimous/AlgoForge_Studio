#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

constexpr uint32_t kTicksPerShift = 60u;

algorithm::AlgorithmContainer* RequireRegisterContainer(
  algorithm::AlgorithmContainerSet* container_set,
  const char* container_name) {
  assert(container_set && "Algorithm container set must be valid.");
  assert(container_name && "Container name must be valid.");
  algorithm::AlgorithmContainer* container =
    algorithm::FindAlgorithmContainer(container_set, container_name ? container_name : "");
  assert(container && "Required algorithm container is missing.");
  assert(
    container->storage_kind == algorithm::AlgorithmContainerStorageKind::TemporaryRegister &&
    "Expected a temporary register container.");
  assert(container->element_stride >= sizeof(uint32_t) && "Register stride must hold uint32_t.");
  assert(container->bytes.size() >= sizeof(uint32_t) && "Register storage must hold uint32_t.");
  return container;
}

uint32_t ReadUint32(const algorithm::AlgorithmContainer& container) {
  assert(container.bytes.size() >= sizeof(uint32_t) && "Register storage must hold uint32_t.");
  uint32_t value = 0u;
  std::memcpy(&value, container.bytes.data(), sizeof(value));
  return value;
}

void WriteUint32(algorithm::AlgorithmContainer* container, uint32_t value) {
  assert(container && "Register container must be valid.");
  assert(container->bytes.size() >= sizeof(uint32_t) && "Register storage must hold uint32_t.");
  std::memcpy(container->bytes.data(), &value, sizeof(value));
}

uint16_t RotateLeft16(uint16_t value) {
  const uint16_t carry = static_cast<uint16_t>((value >> 15u) & 0x1u);
  return static_cast<uint16_t>((value << 1u) | carry);
}

uint32_t RotatePackedGridState(uint32_t packed_state) {
  const uint16_t high_mask = static_cast<uint16_t>((packed_state >> 16u) & 0xFFFFu);
  const uint16_t low_mask = static_cast<uint16_t>(packed_state & 0xFFFFu);
  const uint16_t rotated_high_mask = RotateLeft16(high_mask);
  const uint16_t rotated_low_mask = RotateLeft16(low_mask);
  return (static_cast<uint32_t>(rotated_high_mask) << 16u) |
    static_cast<uint32_t>(rotated_low_mask);
}

class PrecisionGridMaskCpuExecutor final : public agent::IAlgorithmCpuExecutor {
 public:
  bool ExecuteCpuAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    algorithm::AlgorithmContainerSet* algorithm_container_set,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;
    assert(algorithm_container_set && "Algorithm container set must be valid.");

    algorithm::AlgorithmContainer* packed_state_container =
      RequireRegisterContainer(algorithm_container_set, "packed_grid_state");
    algorithm::AlgorithmContainer* tick_counter_container =
      RequireRegisterContainer(algorithm_container_set, "tick_counter");

    uint32_t packed_state = ReadUint32(*packed_state_container);
    uint32_t tick_counter = ReadUint32(*tick_counter_container);
    ++tick_counter;
    if ((tick_counter % kTicksPerShift) == 0u) {
      packed_state = RotatePackedGridState(packed_state);
      WriteUint32(packed_state_container, packed_state);
    }
    WriteUint32(tick_counter_container, tick_counter);

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
        .name = "v2a0_precision_grid_mask_demo.cpu",
        .payload = "tick=" + std::to_string(tick_counter) +
          ", packed_state=" + std::to_string(packed_state),
      });
    }
    return true;
  }
};

void DestroyCpuExecutor(agent::IAlgorithmCpuExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->cpu_symbol = true;
  out_bundle->gpu_symbol = false;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->cpu_executor = new PrecisionGridMaskCpuExecutor();
  out_bundle->destroy_cpu_executor = &DestroyCpuExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithm::AlgorithmReflector* out_reflector) {
  if (!request || !out_reflector) {
    return false;
  }

  std::shared_ptr<algorithm::AlgorithmReflector> runtime_reflector{};
  algorithm::AlgorithmPackageLocation package_location{};
  if (!algorithm::TryResolveAlgorithmPackageLocationForPluginCompile(
        request->algorithm_name ? request->algorithm_name : "",
        &package_location,
        nullptr)) {
    return false;
  }
  if (!algorithmManager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
