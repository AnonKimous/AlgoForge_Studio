#pragma once

#include "../app/module_agents.h"

#include <filesystem>
#include <vector>

namespace app_orchestration {

using agents::PhysAgent;
using agents::RenderAgent;

class AppFrameSyncGlue {
 public:
  static void SyncPhysModeLifecycleFromRenderAgent(
    const RenderAgent& render_agent,
    PhysAgent* phys_agent,
    Mesh* mesh,
    const std::filesystem::path& snapshot_path);

  static RenderUiState BuildRenderUiStateFromRenderAgentAndPhysAgentAndInteraction(
    const RenderAgent& render_agent,
    const PhysAgent& phys_agent,
    const Mesh& mesh,
    const std::filesystem::path& mesh_path,
    const InteractionFrame& interaction,
    float animation_time);

  static void ApplyRenderFrameResultOnPhysAgentAndMesh(
    const RenderFrameResult& frame,
    PhysAgent* phys_agent,
    Mesh* mesh);
};
}  // namespace app_orchestration
