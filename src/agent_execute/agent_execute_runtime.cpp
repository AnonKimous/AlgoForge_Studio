#include "agent_execute_runtime.h"

#include <memory>
#include <utility>

namespace agent_execute {

bool AgentExecuteRuntime::LoadAgent(AgentLaunchSpec spec) {
  auto agent_instance = std::make_shared<agent::Agent>();
  if (!agent::agent_init(agent_instance.get(), std::move(spec.agent_config))) {
    return false;
  }

  loaded_agent_ = std::move(agent_instance);
  agent_ticker_.Destroy();
  agent_ticker_.Init(loaded_agent_);
  return true;
}

bool AgentExecuteRuntime::UnloadAgent() {
  if (!loaded_agent_) {
    return false;
  }
  agent_ticker_.Destroy();
  loaded_agent_.reset();
  return true;
}

bool AgentExecuteRuntime::Tick(Mesh& mesh, const InputState& input, Vec2 mouse_pixel, float dt_seconds) {
  if (!loaded_agent_) {
    return true;
  }
  agent_ticker_.Tick(mesh, input, mouse_pixel, dt_seconds);
  return true;
}

void AgentExecuteRuntime::Destroy() {
  agent_ticker_.Destroy();
  loaded_agent_.reset();
}

}  // namespace agent_execute
