#pragma once

#include "agent_management/agent_manager.h"
#include "common_data/common_data.h"
#include "runtime_systems/runtime_environment.h"

#include <cstddef>
#include <imgui.h>
#include <string>
#include <vector>

namespace interact_ui {

class IInteractUiHost {
 public:
  virtual ~IInteractUiHost() = default;

  virtual AgentManager& agent_manager() = 0;
  virtual const AgentManager& agent_manager() const = 0;
  virtual bool CreateAgent(AgentCreateSpec spec, size_t* out_agent_index = nullptr) = 0;
  virtual bool AttachAlgorithmToAgent(
    size_t agent_index,
    const std::string& algorithm_name,
    const std::vector<agent::AlgorithmResourceBinding>& resource_bindings,
    const std::vector<agent::AlgorithmDescriptorValue>& descriptor_values,
    size_t* out_algorithm_index = nullptr,
    std::string* out_error_message = nullptr,
    agent::AlgorithmMountMode mount_mode = agent::AlgorithmMountMode::Direct,
    agent::AlgorithmExecutionPreference execution_preference = agent::AlgorithmExecutionPreference::Gpu) = 0;
  virtual bool TickManagedAgents() = 0;
  virtual void ClearAgents() = 0;
  virtual void ClearGpuExecutors() = 0;

  virtual const InputState& input() const = 0;
  virtual Vec2 mouse_position() const = 0;
  virtual float frame_dt_seconds() const = 0;
  virtual bool has_render_preview_texture() const = 0;
  virtual ImTextureID render_preview_texture_id() const = 0;
  virtual ImVec2 render_preview_texture_size() const = 0;
  virtual void SetRenderPreviewExtent(ImVec2 extent) = 0;
  virtual void SetRenderPreviewRequest(runtime_systems::RenderPreviewRequest request) = 0;

  virtual std::string& ui_status_message() = 0;
  virtual const std::string& ui_status_message() const = 0;
};

}  // namespace interact_ui

