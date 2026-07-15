#define ALGORITHM_LIBRARY_PLUGIN_BUILD 1

#include "../algorithm_plugin_api.h"

#include <cassert>
#include <cstdint>
#include <string>

namespace {

struct OpaqueThirdPartyState {
  uint64_t tick_count{0u};
};

class CompatibilityProbeExecutor final : public agent::IAlgorithmCompatibilityExecutor {
 public:
  bool ExecuteCompatibleAlgorithm(
    const agent::AgentTickContext& context,
    const algorithm::AlgorithmProfile& algorithm_profile,
    const AgentToAlgorithmSignal& agent_to_algorithm_signal,
    AlgorithmToAgentSignal* algorithm_to_agent_signal,
    agent::AlgorithmPackageDebugState* debug_state) override {
    (void)context;
    (void)algorithm_profile;
    (void)agent_to_algorithm_signal;

    state_.tick_count += 1u;
    algorithm_to_agent_signal->control_bits = static_cast<uint32_t>(state_.tick_count);
    debug_state->signals.push_back(algorithm_management::AdvancedAlgorithmDebugSignal{
      .name = "compatibility_probe.opaque_state",
      .payload = "tick=" + std::to_string(state_.tick_count),
    });
    return true;
  }

 private:
  OpaqueThirdPartyState state_{};
};

void DestroyCompatibilityProbeExecutor(agent::IAlgorithmCompatibilityExecutor* executor) {
  delete executor;
}

}  // namespace

extern "C" ALGORITHM_LIBRARY_PLUGIN_API bool AlgorithmPlugin_CreateBundle(
  const algorithmManager::support::AlgorithmPluginRequest* request,
  algorithmManager::support::AlgorithmPluginBundle* out_bundle) {
  (void)request;
  assert(out_bundle && "Algorithm plugin bundle output must be valid.");

  out_bundle->Clear();
  out_bundle->jobs_symbol = false;
  out_bundle->vk_symbol = false;
  out_bundle->cuda_symbol = false;
  out_bundle->compatibility_symbol = true;
  out_bundle->reflector = false;
  out_bundle->intervention = true;
  out_bundle->compatibility_executor = new CompatibilityProbeExecutor();
  out_bundle->destroy_compatibility_executor = &DestroyCompatibilityProbeExecutor;
  return true;
}
