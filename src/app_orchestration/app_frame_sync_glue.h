#pragma once

#include "agents/agents.h"
#include "interaction_analysis/interaction_agents.h"

namespace app_orchestration {

using agents::RenderAgent;
using interaction_analysis::PhysAgent;

class AppFrameSyncGlue {
 public:
  static RenderUiState BuildRenderUiStateFromRenderAgentAndPhysAgent(
    const RenderAgent& render_agent,
    const PhysAgent& phys_agent,
    float animation_time);

  static void ApplyRenderFrameResultOnPhysAgentAndMesh(
    const RenderFrameResult& frame,
    PhysAgent* phys_agent,
    Mesh* mesh);
};
}  // namespace app_orchestration
