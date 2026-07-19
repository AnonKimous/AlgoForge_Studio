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

uint32_t RotateSplitMask(uint32_t value) {
  const uint16_t high_mask = RotateLeft16(static_cast<uint16_t>(value >> 16u));
  const uint16_t low_mask = RotateLeft16(static_cast<uint16_t>(value & 0xFFFFu));
  return (static_cast<uint32_t>(high_mask) << 16u) | static_cast<uint32_t>(low_mask);
}

class PrecisionGridMaskJobsExecutor final : public agent::IAlgorithmJobsExecutor {
 public:
  bool ExecuteJobsAlgorithm(
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

    algorithm::AlgorithmContainer* grid_mask_container =
      RequireRegisterContainer(algorithm_container_set, "v1");
    algorithm::AlgorithmContainer* tick_counter_container =
      RequireRegisterContainer(algorithm_container_set, "v2");

    uint32_t grid_mask = ReadUint32(*grid_mask_container);
    uint32_t tick_counter = ReadUint32(*tick_counter_container);
    ++tick_counter;
    if ((tick_counter % kTicksPerShift) == 0u) {
      grid_mask = RotateSplitMask(grid_mask);
      WriteUint32(grid_mask_container, grid_mask);
    }
    WriteUint32(tick_counter_container, tick_counter);

    if (algorithm_to_agent_signal) {
      *algorithm_to_agent_signal = {};
    }
    if (debug_state) {
      const uint32_t high_mask = grid_mask >> 16u;
      const uint32_t low_mask = grid_mask & 0xFFFFu;
      debug_state->signals.push_back(algomanager::algoscheduler::AdvancedAlgorithmDebugSignal{
        .name = "v2a0_precision_grid_mask_demo.jobs",
        .payload = "tick=" + std::to_string(tick_counter) +
          ", grid_mask=" + std::to_string(grid_mask) +
          ", high_mask=" + std::to_string(high_mask) +
          ", low_mask=" + std::to_string(low_mask) +
          ", grid_mask_state32=" + std::to_string(grid_mask),
      });
    }
    return true;
  }
};

void DestroyJobsExecutor(agent::IAlgorithmJobsExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algomanager::support::AlgorithmPluginRequest* request,
  algomanager::support::AlgorithmPluginBundle* out_bundle) {
  if (!request || !out_bundle) {
    return false;
  }

  out_bundle->Clear();
  out_bundle->jobs_symbol = true;
  out_bundle->vk_symbol = false;
  out_bundle->reflector = true;
  out_bundle->intervention = true;
  out_bundle->jobs_executor = new PrecisionGridMaskJobsExecutor();
  out_bundle->destroy_jobs_executor = &DestroyJobsExecutor;
  return true;
}

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateRuntimeReflector(
  const algomanager::support::AlgorithmPluginRequest* request,
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
  if (!algomanager::support::LoadAlgorithmPackageReflectorFromLocation(
        package_location,
        &runtime_reflector,
        nullptr) || !runtime_reflector) {
    return false;
  }
  *out_reflector = *runtime_reflector;
  return true;
}
