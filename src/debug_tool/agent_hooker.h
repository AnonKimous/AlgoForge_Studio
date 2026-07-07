#pragma once

#include "agent_management/agent_management.h"

#include <memory>
#include <string>

namespace debug_tool_backend::agent_hooker {

inline size_t AlgorithmCount(const agentmanager::agent::Agent& agent) {
  return agent.algorithm_count();
}

inline const agentmanager::agent::AlgorithmObject* AlgorithmObjectAt(const agentmanager::agent::Agent& agent, size_t index) {
  return agent.algorithm_object(index);
}

inline const agentmanager::agent::AgentAlgorithmRuntimeState* AlgorithmRuntimeStateAt(
  const agentmanager::agent::Agent& agent,
  size_t index) {
  return agent.algorithm_runtime_state(index);
}

inline const std::string& AgentName(const agentmanager::agent::Agent& agent) {
  return agent.agent_name();
}

inline bool BeginAlgorithmAssembly(agentmanager::agent::Agent& agent, size_t index) {
  return agent.BeginAlgorithmAssembly(index);
}

inline const ::algorithm::AlgorithmContainerSet* ContainerSet(const agentmanager::agent::AlgorithmObject& object) {
  return object.container_set();
}

}  // namespace debug_tool_backend::agent_hooker

