#include "interaction/edit_mode_controller.h"
#include "interaction/interaction_module.h"
#include "interaction/phys_mode_controller.h"
#include "mesh.h"
#include "render/render_module.h"
#include "window/window.h"
#include "triangle_orientation_analyzer.h"
#include "validation_bridge/validation_bridge.h"
#include "validation_layer/validation_layer.h"
#include "viewport_transform.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv) {
  try {
    std::vector<std::string> args;
    args.reserve(argc);
    for (int i = 0; i < argc; ++i) {
      args.emplace_back(argv[i] ? argv[i] : "");
    }
    const bool validation_layer_enabled = ValidationLayerApi::ParseStartupFlag(args);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    if (!std::filesystem::exists(MESH_PATH)) {
      GenerateSubdividedTriangleMeshFile(MESH_PATH);
    }
    std::filesystem::path mesh_path = MESH_PATH;
    std::filesystem::path snapshot_path = mesh_path;
    snapshot_path += ".meshts";
    Mesh reference_mesh;
    Mesh mesh = LoadMeshWithSnapshot(mesh_path.string(), snapshot_path.string(), &reference_mesh);

    SdlWindow window("Vulkan Mesh Vertex Editor", 1280, 720);
    VulkanRenderer renderer(window.native_handle());
    ViewportTransform viewport;
    TriangleOrientationAnalyzer orientation;
    EditModeController edit_controller;
    PhysModeController phys_controller;
    ValidationLayerApi validation_layer;
    if (validation_layer_enabled) {
      validation_layer.Start();
    }

    InteractionFrame interaction{};
    InteractionMode previous_mode = renderer.mode();
    auto start_time = std::chrono::steady_clock::now();
    auto last_frame_time = start_time;
    while (window.ProcessEvents()) {
      auto now = std::chrono::steady_clock::now();
      float frame_dt = std::chrono::duration<float>(now - last_frame_time).count();
      if (frame_dt < 0.0f) frame_dt = 0.0f;
      if (frame_dt > 0.05f) frame_dt = 0.05f;
      last_frame_time = now;
      SceneViewBounds scene_bounds = renderer.scene_view_bounds();
      Vec2 mouse_pixel = window.MousePosition();
      if (scene_bounds.valid) {
        mouse_pixel.x -= scene_bounds.x;
        mouse_pixel.y -= scene_bounds.y;
        viewport.SetSize(static_cast<int>(scene_bounds.width), static_cast<int>(scene_bounds.height));
      } else {
        viewport.SetSize(window.width(), window.height());
      }
      InteractionMode mode = renderer.mode();
      if (mode == InteractionMode::Phys && previous_mode != InteractionMode::Phys) {
        phys_controller.PhysInit(mesh);
        if (!std::filesystem::exists(snapshot_path)) {
          SaveMeshSnapshotFile(mesh, snapshot_path.string());
        }
      }
      previous_mode = mode;

      phys_controller.SetRunState(renderer.phys_run_state());
      phys_controller.SetGuideEnabled(renderer.phys_guide_enabled());
      interaction = mode == InteractionMode::Edit
        ? edit_controller.Tick(mesh, viewport, window.input(), mouse_pixel)
        : phys_controller.Tick(mesh, viewport, window.input(), mouse_pixel, frame_dt);

      orientation.Tick(mesh, reference_mesh);
      RenderUiState ui{};
      ui.mode = mode;
      ui.phys_run_state = renderer.phys_run_state();
      ui.phys_guide_enabled = renderer.phys_guide_enabled();
      ui.selected_phys_directive = phys_controller.selected_directive();
      ui.phys_current_frame_index = phys_controller.current_frame_index();
      ui.mesh_file_name = mesh_path.filename().string();
      ui.selection = interaction.selection;
      ui.vertex_deltas = phys_controller.vertex_deltas();
      ui.active_phys_directives = phys_controller.active_directives();
      ui.recorded_frames = phys_controller.recorded_frames();
      ui.guide_keyframes = phys_controller.guide_keyframes();
      ui.animation_time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();
      if (interaction.selection.kind == SelectionKind::Vertex && interaction.selection.vertex >= 0) {
        ui.selected_vertex_position = mesh.positions[interaction.selection.vertex];
      }
      if (interaction.selection.kind == SelectionKind::Triangle && interaction.selection.triangle >= 0 &&
          static_cast<size_t>(interaction.selection.triangle) < mesh.triangle_material_gpa.size()) {
        ui.selected_triangle_material_gpa = mesh.triangle_material_gpa[interaction.selection.triangle];
      }

      RenderFrameResult frame = renderer.Draw(mesh, orientation, interaction.highlighted_vertex, ui);
      ValidationFrameSnapshot validation_snapshot = BuildValidationFrameSnapshot(
        mesh,
        reference_mesh,
        orientation,
        ui,
        frame,
        interaction.highlighted_vertex,
        frame_dt);
      validation_layer.PublishFrame(validation_snapshot);
      if (frame.phys_state_restore_requested && frame.phys_state_restore_index >= 0) {
        phys_controller.RestoreRecordedFrame(mesh, frame.phys_state_restore_index);
      }
      if (frame.phys_state_cache_requested) {
        phys_controller.CacheCurrentState();
      }
      if (frame.phys_step_requested) {
        phys_controller.StepOnce(mesh);
      }
      if (frame.recorded_frame_toggle_requested && frame.recorded_frame_toggle_index >= 0) {
        phys_controller.SetRecordedFrameExpanded(frame.recorded_frame_toggle_index, frame.recorded_frame_toggle_expanded);
      }
      if (frame.guide_keyframe_toggle_requested && frame.guide_keyframe_toggle_index >= 0) {
        const auto& guide_keyframes = phys_controller.guide_keyframes();
        if (frame.guide_keyframe_toggle_index < static_cast<int>(guide_keyframes.size())) {
          phys_controller.SetGuideKeyframeEnabled(guide_keyframes[frame.guide_keyframe_toggle_index].frame_index, frame.guide_keyframe_toggle_enabled);
        }
      }
      if (frame.guide_keyframe_expand_requested && frame.guide_keyframe_expand_index >= 0) {
        phys_controller.SetGuideKeyframeExpandedByIndex(frame.guide_keyframe_expand_index, frame.guide_keyframe_expand_expanded);
      }
      if (frame.triangle_material_change_requested && frame.triangle_material_triangle >= 0 &&
          static_cast<size_t>(frame.triangle_material_triangle) < mesh.triangle_material_gpa.size()) {
        mesh.triangle_material_gpa[frame.triangle_material_triangle] = frame.triangle_material_gpa > 0.0f ? frame.triangle_material_gpa : 0.001f;
      }
      if (frame.save_requested) {
        SaveMeshSnapshotFile(mesh, snapshot_path.string());
      }
    }
    validation_layer.Stop();
    SDL_Quit();
    return 0;
  } catch (const std::exception& e) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mesh editor error", e.what(), nullptr);
    SDL_Quit();
    return 1;
  }
}
