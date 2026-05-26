#pragma once

#include "../math_types.h"
#include "../physics/physics_types.h"

#include <cstdint>
#include <string>
#include <vector>

enum class InteractionMode {
  Edit,
  Phys,
};

enum class SelectionKind {
  None,
  Vertex,
  Triangle,
};

struct SelectionState {
  SelectionKind kind{SelectionKind::None};
  int vertex{-1};
  int triangle{-1};
};

struct InteractionFrame {
  int highlighted_vertex{-1};
  SelectionState selection{};
};

struct RenderUiState {
  InteractionMode mode{InteractionMode::Edit};
  PhysSubMode phys_sub_mode{PhysSubMode::Guide};
  std::string mesh_file_name;
  SelectionState selection{};
  Vec3 selected_vertex_position{};
  std::vector<PhysDirective> phys_directives;
  float animation_time{};
};

struct RenderFrameResult {
  uint32_t draw_calls{};
  InteractionMode mode{InteractionMode::Edit};
  PhysSubMode phys_sub_mode{PhysSubMode::Guide};
  bool save_requested{};
};
