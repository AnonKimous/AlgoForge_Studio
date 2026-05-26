#include "interaction/interaction_module.h"
#include "mesh.h"
#include "render/render_module.h"
#include "triangle_orientation_analyzer.h"
#include "viewport_transform.h"
#include "win32_window.h"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

int WINAPI wWinMain(HINSTANCE hinstance, HINSTANCE, PWSTR, int) {
  try {
    if (!std::filesystem::exists(MESH_PATH)) {
      GenerateSubdividedTriangleMeshFile(MESH_PATH);
    }
    std::filesystem::path mesh_path = MESH_PATH;
    std::filesystem::path snapshot_path = mesh_path;
    snapshot_path += ".meshts";
    Mesh reference_mesh;
    Mesh mesh = LoadMeshWithSnapshot(mesh_path.string(), snapshot_path.string(), &reference_mesh);

    Win32Window window(hinstance, 1280, 720);
    VulkanRenderer renderer(window);
    ViewportTransform viewport;
    TriangleOrientationAnalyzer orientation;
    EditModeController edit_controller;
    PhysModeController phys_controller;

    InteractionFrame interaction{};
    InteractionMode previous_mode = renderer.mode();
    auto start_time = std::chrono::steady_clock::now();
    while (window.ProcessMessages()) {
      viewport.SetSize(window.width(), window.height());
      Vec2 mouse_pixel = window.MousePosition();
      InteractionMode mode = renderer.mode();
      if (mode == InteractionMode::Phys && previous_mode != InteractionMode::Phys) {
        phys_controller.PhysInit(mesh);
      }
      previous_mode = mode;

      phys_controller.SetSubMode(renderer.phys_sub_mode());
      interaction = mode == InteractionMode::Edit
        ? edit_controller.Tick(mesh, viewport, window.input(), mouse_pixel)
        : phys_controller.Tick(mesh, viewport, window.input(), mouse_pixel);

      orientation.Tick(mesh, reference_mesh);
      RenderUiState ui{};
      ui.mode = mode;
      ui.phys_sub_mode = renderer.phys_sub_mode();
      ui.mesh_file_name = mesh_path.filename().string();
      ui.selection = interaction.selection;
      ui.phys_directives = phys_controller.directives();
      ui.animation_time = std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count();
      if (interaction.selection.kind == SelectionKind::Vertex && interaction.selection.vertex >= 0) {
        ui.selected_vertex_position = mesh.positions[interaction.selection.vertex];
      }

      RenderFrameResult frame = renderer.Draw(mesh, orientation, interaction.highlighted_vertex, ui);
      if (frame.save_requested) {
        SaveMeshSnapshotFile(mesh, snapshot_path.string());
      }
    }
    return 0;
  } catch (const std::exception& e) {
    MessageBoxA(nullptr, e.what(), "Mesh editor error", MB_OK | MB_ICONERROR);
    return 1;
  }
}
