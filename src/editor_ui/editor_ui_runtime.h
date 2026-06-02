#pragma once

#include "entity_interaction/entity_interaction_agents.h"

#include <cstddef>

namespace editor_ui {

class EditorUiRuntime {
 public:
  bool Init(const Mesh& mesh, const char* window_title, int width, int height);
  bool LoadMeshFromFile(const std::string& path, std::string* error_message = nullptr);
  bool Tick();
  void Destroy();

 private:
  struct EntityDraftState {
    char entity_name[128]{};
    char algorithm_name[128]{};
    char mesh_path[512]{};
    char gpu_shader_path[512]{};
    int preset_index{1};
    int solver_kind_index{0};
    bool load_mesh_before_create{true};
  };

  void ResetEntityDraftState();
  void DrawLoadedEntityListUi();
  void DrawEntityDraftUi();
  void DrawEditorUi();
  bool LoadMeshAndResetBindings(const std::string& path, std::string* status_message);
  bool CreateEntityFromDraft(std::string* status_message);

  entity_interaction::EntityInteractionRuntime entity_runtime_{};
  EntityDraftState entity_draft_{};
  std::size_t selected_entity_slot_{static_cast<std::size_t>(-1)};
};

}  // namespace editor_ui

using editor_ui::EditorUiRuntime;
