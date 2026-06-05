#include "agent_manager.h"

#define AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE 1
#include "agent_management/agent_ticker.h"
#undef AGENT_MANAGEMENT_LAYER_PUBLIC_FACADE_INCLUDE

#include <memory>
#include <utility>

namespace agent_management {

struct AgentManager::ManagedAgentEntry {
  std::shared_ptr<agent::Agent> agent{};
  AgentTicker ticker{};
};

AgentManager::AgentManager() = default;

AgentManager::~AgentManager() {
  Destroy();
}

bool AgentManager::CreateAgent(AgentCreateSpec spec, size_t* out_agent_index) {
  auto agent_instance = std::make_shared<agent::Agent>();
  if (!agent::agent_init(agent_instance.get(), std::move(spec.agent_config))) {
    return false;
  }

  auto managed_agent = std::make_shared<ManagedAgentEntry>();
  managed_agent->agent = std::move(agent_instance);
  managed_agent->ticker.Init(managed_agent->agent);

  managed_agents_.push_back(std::move(managed_agent));
  if (out_agent_index) {
    *out_agent_index = managed_agents_.size() - 1u;
  }
  return true;
}

bool AgentManager::DestroyAgent(size_t agent_index) {
  if (agent_index >= managed_agents_.size()) {
    return false;
  }

  managed_agents_[agent_index]->ticker.Destroy();
  managed_agents_.erase(managed_agents_.begin() + static_cast<std::ptrdiff_t>(agent_index));
  return true;
}

void AgentManager::ClearAgents() {
  for (std::shared_ptr<ManagedAgentEntry>& managed_agent : managed_agents_) {
    if (managed_agent) {
      managed_agent->ticker.Destroy();
    }
  }
  managed_agents_.clear();
  combined_algorithm_to_agent_signal_ = {};
}

bool AgentManager::Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  combined_algorithm_to_agent_signal_ = {};
  for (std::shared_ptr<ManagedAgentEntry>& managed_agent : managed_agents_) {
    if (!managed_agent) {
      continue;
    }
    managed_agent->ticker.Tick(mesh, input, mouse_pixel, dt_seconds);
    const AlgorithmToAgentSignal& signal = managed_agent->ticker.algorithm_to_agent_signal();
    combined_algorithm_to_agent_signal_.intervention_applied =
      combined_algorithm_to_agent_signal_.intervention_applied || signal.intervention_applied;
    combined_algorithm_to_agent_signal_.pause_requested =
      combined_algorithm_to_agent_signal_.pause_requested || signal.pause_requested;
    combined_algorithm_to_agent_signal_.stop_requested =
      combined_algorithm_to_agent_signal_.stop_requested || signal.stop_requested;
    combined_algorithm_to_agent_signal_.intervention_needed =
      combined_algorithm_to_agent_signal_.intervention_needed || signal.intervention_needed;
  }
  return true;
}

void AgentManager::Destroy() {
  ClearAgents();
}

size_t AgentManager::agent_count() const {
  return managed_agents_.size();
}

bool AgentManager::has_agents() const {
  return !managed_agents_.empty();
}

std::shared_ptr<agent::Agent> AgentManager::agent(size_t index) const {
  if (index >= managed_agents_.size() || !managed_agents_[index]) {
    return {};
  }
  return managed_agents_[index]->agent;
}

}  // namespace agent_management
