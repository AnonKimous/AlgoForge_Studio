#pragma once

#include "agent/agent.h"

#include <memory>
#include <string>

namespace debug_tool_backend::agent_hooker {

inline size_t AlgorithmCount(const agent::Agent& agent) {
  return agent.algorithm_count();
}

inline const agent::AlgorithmObject* AlgorithmObjectAt(const agent::Agent& agent, size_t index) {
  return agent.algorithm_object(index);
}

inline const agent::AgentAlgorithmRuntimeState* AlgorithmRuntimeStateAt(
  const agent::Agent& agent,
  size_t index) {
  return agent.algorithm_runtime_state(index);
}

inline const std::string& AgentName(const agent::Agent& agent) {
  return agent.agent_name();
}

inline bool BeginAlgorithmAssembly(agent::Agent& agent, size_t index) {
  return agent.BeginAlgorithmAssembly(index);
}

inline const ::algorithm::AlgorithmContainerSet* ContainerSet(const agent::AlgorithmObject& object) {
  return object.container_set();
}

}  // namespace debug_tool_backend::agent_hooker
