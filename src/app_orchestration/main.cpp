#include "algorithm/legacy_corotated_cpu_algorithm_contract.h"
#include "algorithm/physics_convolution_gpu_algorithm_contract.h"
#include "agents/agents.h"
#include "decomposition/decomposition_manager.h"
#include "messaging/io_bus.h"
#include "app_orchestration/app_frame_sync_glue.h"
#include "app_orchestration/guide_ui_frame_relay_on_phys_agent_buffers.h"
#include "interaction_analysis/interaction_module.h"
#include "data_protocol/mesh.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    using agents::IoBusAgent;
    using agents::RenderAgent;
    using agents::SceneViewAgent;
    using agents::SceneViewFrameState;
    using agents::WindowAgent;
    using data_protocol::InteractionFrame;
    using data_protocol::InteractionMode;
    using interaction_analysis::EditModeAgent;
    using interaction_analysis::GuideUiAgent;
    using interaction_analysis::TriangleAnalysisAgent;
    using interaction_analysis::PhysAgent;

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
    SceneViewAgent scene_view_agent;
    TriangleAnalysisAgent triangle_analysis_agent;
    EditModeAgent edit_agent;
    GuideUiAgent guide_ui_agent;
    IoBusAgent io_bus_agent;
    io_bus_agent.Init();
    auto guide_ui_phys_channel = io_bus_agent.AllocateFastChannel("guide_ui_phys");
    RenderAgent render_agent;
    render_agent.Init(window_agent.native_handle());
    scene_view_agent.Init(mesh);
    triangle_analysis_agent.Init(reference_mesh);
    edit_agent.Init();
    guide_ui_agent.Init();
    CreatePhysSolverInfo phys_solver_info{};
    phys_solver_info.solver_kind = PhysSolverKind::Cpu;
    phys_solver_info.algorithm_name = kLegacyCorotatedCpuAlgorithmName;
    phys_solver_info.run_algorithm_on_init = true;
    phys_solver_info.gpu_shader.shader_name = SHADER_PHYSICS_CONV_PATH;
    phys_solver_info.gpu_shader.shader_mask.assign(9, 1u);
    phys_solver_info.gpu_shader.shader_data.assign(9, 1.0f);
    if (phys_solver_info.solver_kind != PhysSolverKind::Cpu) {
      phys_solver_info.algorithm_name = kPhysicsConvolutionGpuAlgorithmName;
    }
    AlgorithmComplianceDescriptor algorithm_compliance_descriptor{};
    if (phys_solver_info.algorithm_name == kLegacyCorotatedCpuAlgorithmName) {
      algorithm_compliance_descriptor = CreateLegacyCorotatedCpuAlgorithmComplianceDescriptor(
        static_cast<uint32_t>(mesh.positions.size()),
        static_cast<uint32_t>(mesh.triangles.size()));
    } else if (phys_solver_info.algorithm_name == kPhysicsConvolutionGpuAlgorithmName) {
      algorithm_compliance_descriptor = CreatePhysicsConvolutionGpuAlgorithmComplianceDescriptor(
        static_cast<uint32_t>(mesh.positions.size()));
    }
    PhysAgent phys_agent;
    phys_agent.Init(
      phys_solver_info,
      render_agent.compute_context(),
      algorithm_compliance_descriptor,
      guide_ui_phys_channel[0]);
    decomposition::CodecManager guide_ui_codec{};

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
      app_orchestration::AppFrameSyncGlue::SyncPhysModeLifecycleFromRenderAgent(render_agent, &phys_agent, &mesh, snapshot_path);
      phys_agent.SetRunState(render_agent.phys_run_state());
      phys_agent.SetGuideEnabled(render_agent.phys_guide_enabled());
      phys_agent.ResolveIncomingIoBuffers(mesh);

      GuideUiFrame guide_ui_frame{};
      if (mode == InteractionMode::Phys) {
        guide_ui_frame = guide_ui_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel);
        decomposition::GuideUiPhysDescriptor guide_descriptor = app_orchestration::BuildGuideUiFrameRelayOnPhysAgentDescriptor(
          mesh,
          guide_ui_frame,
          phys_agent.guide_edit_mode(),
          phys_agent.guide_velocity_magnitude(),
          phys_agent.guide_velocity_delay_frames(),
          phys_agent.guide_velocity_duration_frames(),
          phys_agent.guide_force_magnitude(),
          phys_agent.guide_force_delay_frames(),
          phys_agent.guide_force_duration_frames());
        phys_agent.SetSelectedGuideVertices(guide_descriptor.guide_ui_frame.selected_vertices);
        IoBufferPacket guide_packet = guide_ui_codec.BuildGuideUiPhysPacket(guide_descriptor);
        io_bus_agent.PublishToFastChannel("guide_ui_phys", guide_packet);
        phys_agent.ResolveIncomingIoBuffers(mesh);
      }
      interaction = mode == InteractionMode::Edit
        ? edit_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel)
        : phys_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel, frame_dt);

      triangle_analysis_agent.Tick(mesh);
      RenderUiState ui = app_orchestration::AppFrameSyncGlue::BuildRenderUiStateFromRenderAgentAndPhysAgentAndInteraction(
        render_agent,
        phys_agent,
        mesh,
        mesh_path,
        interaction,
        std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count());

      RenderFrameResult frame = render_agent.Tick(mesh, triangle_analysis_agent.state(), scene_view_agent.camera(), interaction.highlighted_vertex, ui);
      app_orchestration::AppFrameSyncGlue::ApplyRenderFrameResultOnPhysAgentAndMesh(frame, &phys_agent, &mesh);
      if (frame.save_requested) {
        SaveMeshSnapshotFile(mesh, snapshot_path.string());
      }
    }
    triangle_analysis_agent.Destroy();
    io_bus_agent.Destroy();
    phys_agent.Destroy();
    guide_ui_agent.Destroy();
    edit_agent.Destroy();
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
