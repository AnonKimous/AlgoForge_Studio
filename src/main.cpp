#include "algorithm/legacy_corotated_cpu_algorithm_contract.h"
#include "algorithm/physics_convolution_gpu_algorithm_contract.h"
#include "app/module_agents.h"
#include "communication/io_protocol.h"
#include "glue/app_frame_sync_glue.h"
#include "glue/guide_ui_frame_relay_on_phys_manager_buffers.h"
#include "interaction/interaction_module.h"
#include "mesh.h"
#include "validation_bridge/validation_bridge.h"

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
    using namespace agents;
    using namespace app_orchestration;
    using namespace interaction_analysis;

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

    WindowAgent window_agent;
    window_agent.Init("Vulkan Mesh Vertex Editor", 1280, 720);
    RenderAgent render_agent;
    render_agent.Init(window_agent.native_handle());
    SceneViewAgent scene_view_agent;
    scene_view_agent.Init(mesh);
    TriangleAnalysisAgent triangle_analysis_agent;
    triangle_analysis_agent.Init(reference_mesh);
    EditModeAgent edit_agent;
    edit_agent.Init();
    GuideUiAgent guide_ui_agent;
    guide_ui_agent.Init();
    IoBusAgent io_bus_agent;
    io_bus_agent.Init();
    CreatePhysSolverInfo phys_solver_info{};
    phys_solver_info.solver_kind = PhysSolverKind::Cpu;
    phys_solver_info.algorithm_name = kLegacyCorotatedCpuAlgorithmName;
    phys_solver_info.run_algorithm_on_init = true;
    phys_solver_info.gpu_shader.shader_name = SHADER_PHYSICS_CONV_PATH;
    phys_solver_info.gpu_shader.shader_mask.assign(9, 1u);
    phys_solver_info.gpu_shader.shader_data.assign(9, 1.0f);
    if (phys_solver_info.solver_kind == PhysSolverKind::Cpu) {
      phys_solver_info.data_reflection_info = CreateLegacyCorotatedCpuDataReflectionInfo(
        static_cast<uint32_t>(mesh.positions.size()),
        static_cast<uint32_t>(mesh.triangles.size()));
    } else {
      phys_solver_info.algorithm_name = kPhysicsConvolutionGpuAlgorithmName;
      phys_solver_info.data_reflection_info = CreatePhysicsConvolutionGpuDataReflectionInfo(16u);
    }
    PhysAgent phys_agent;
    phys_agent.Init(phys_solver_info, render_agent.compute_context());
    ValidationAgent validation_agent;
    validation_agent.Init(validation_layer_enabled);
    io_bus_agent.BindSharedEndpoint("phys_agent", phys_agent.io_endpoint());
    io_bus_agent.BindSharedEndpoint("render_agent", render_agent.io_endpoint());
    io_bus_agent.BindSharedEndpoint("validation_agent", validation_agent.io_endpoint());
    io_bus_agent.BindGuideUiDirectLine(phys_agent.io_endpoint());

    InteractionFrame interaction{};
    auto start_time = std::chrono::steady_clock::now();
    auto last_frame_time = start_time;
    while (window_agent.Tick()) {
      auto now = std::chrono::steady_clock::now();
      float frame_dt = std::chrono::duration<float>(now - last_frame_time).count();
      if (frame_dt < 0.0f) frame_dt = 0.0f;
      if (frame_dt > 0.05f) frame_dt = 0.05f;
      last_frame_time = now;
      SceneViewFrameState scene_view_frame = scene_view_agent.Tick(render_agent.scene_view_bounds(), window_agent);
      InteractionMode mode = render_agent.mode();
      AppFrameSyncGlue::SyncPhysModeLifecycleFromRenderAgent(render_agent, &phys_agent, &mesh, snapshot_path);

      const std::vector<ValidationAction> validation_actions = validation_agent.Tick();
      if (!validation_actions.empty()) {
        IoBufferPacket validation_packet = BuildValidationActionsIoPacket(validation_actions);
        io_bus_agent.PublishToSharedEndpoint("render_agent", validation_packet);
        render_agent.ResolveIncomingIoBuffers();
        io_bus_agent.PublishToSharedEndpoint("phys_agent", validation_packet);
        phys_agent.ResolveIncomingIoBuffers(mesh);
      }
      IoBufferPacket runtime_control_packet = BuildPhysRuntimeControlIoPacket(
        render_agent.phys_run_state(),
        render_agent.phys_guide_enabled());
      io_bus_agent.PublishToSharedEndpoint("phys_agent", runtime_control_packet);
      phys_agent.ResolveIncomingIoBuffers(mesh);

      GuideUiFrame guide_ui_frame{};
      if (mode == InteractionMode::Phys) {
        guide_ui_frame = guide_ui_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel);
        GuideUiPhysBufferCommit guide_commit = BuildGuideUiFrameRelayOnPhysManagerBuffers(
          mesh,
          guide_ui_frame,
          phys_agent.guide_edit_mode(),
          phys_agent.guide_velocity_magnitude(),
          phys_agent.guide_velocity_delay_frames(),
          phys_agent.guide_velocity_duration_frames(),
          phys_agent.guide_force_magnitude(),
          phys_agent.guide_force_delay_frames(),
          phys_agent.guide_force_duration_frames());
        phys_agent.SetSelectedGuideVertices(guide_commit.selected_vertices);
        io_bus_agent.PublishGuideUiDirectLine(guide_commit.packet);
        phys_agent.ResolveIncomingIoBuffers(mesh);
      }
      interaction = mode == InteractionMode::Edit
        ? edit_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel)
        : phys_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel, frame_dt);

      triangle_analysis_agent.Tick(mesh);
      RenderUiState ui = AppFrameSyncGlue::BuildRenderUiStateFromRenderAgentAndPhysAgentAndInteraction(
        render_agent,
        phys_agent,
        mesh,
        mesh_path,
        interaction,
        std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count());

      RenderFrameResult frame = render_agent.Tick(mesh, triangle_analysis_agent.state(), scene_view_agent.camera(), interaction.highlighted_vertex, ui);
      ValidationFrameSnapshot validation_snapshot = BuildValidationFrameSnapshot(
        mesh,
        reference_mesh,
        triangle_analysis_agent.state(),
        ui,
        frame,
        interaction.highlighted_vertex,
        frame_dt);
      validation_agent.PublishFrame(validation_snapshot);
      AppFrameSyncGlue::ApplyRenderFrameResultOnPhysAgentAndMesh(frame, &phys_agent, &mesh);
      if (frame.save_requested) {
        SaveMeshSnapshotFile(mesh, snapshot_path.string());
      }
    }
    triangle_analysis_agent.Destroy();
    io_bus_agent.Destroy();
    phys_agent.Destroy();
    guide_ui_agent.Destroy();
    edit_agent.Destroy();
    validation_agent.Destroy();
    scene_view_agent.Destroy();
    render_agent.Destroy();
    window_agent.Destroy();
    SDL_Quit();
    return 0;
  } catch (const std::exception& e) {
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Mesh editor error", e.what(), nullptr);
    SDL_Quit();
    return 1;
  }
}
