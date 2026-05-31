#include "algorithm/corotated_cpu_algorithm_contract.h"
#include "algorithm/physics_convolution_gpu_algorithm_contract.h"
#include "agents/agents.h"
#include "app_orchestration/app_frame_sync_glue.h"
#include "interaction_analysis/interaction_agents.h"
#include "common_data/mesh.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

int main(int argc, char** argv) {
  try {
    using agents::RenderAgent;
    using agents::SceneViewAgent;
    using agents::SceneViewFrameState;
    using agents::WindowAgent;
    using interaction_analysis::PhysAgent;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
      throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    if (!std::filesystem::exists(MESH_PATH)) {
      GenerateSubdividedTriangleMeshFile(MESH_PATH);
    }
    std::filesystem::path mesh_path = MESH_PATH;
    Mesh mesh = LoadMeshFile(mesh_path.string());

    WindowAgent window_agent;
    window_agent.Init("Vulkan Mesh Vertex Editor", 1280, 720);
    SceneViewAgent scene_view_agent;
    RenderAgent render_agent;
    render_agent.Init(window_agent.native_handle());
    scene_view_agent.Init(mesh);
    CreatePhysSolverInfo phys_solver_info{};
    phys_solver_info.solver_kind = PhysSolverKind::Cpu;
    phys_solver_info.algorithm_name = kCorotatedCpuAlgorithmName;
    phys_solver_info.run_algorithm_on_init = true;
    phys_solver_info.gpu_shader.shader_name = SHADER_PHYSICS_CONV_PATH;
    phys_solver_info.gpu_shader.shader_mask.assign(9, 1u);
    phys_solver_info.gpu_shader.shader_data.assign(9, 1.0f);
    if (phys_solver_info.solver_kind != PhysSolverKind::Cpu) {
      phys_solver_info.algorithm_name = kPhysicsConvolutionGpuAlgorithmName;
    }
    AlgorithmComplianceDescriptor algorithm_compliance_descriptor{};
    if (phys_solver_info.algorithm_name == kCorotatedCpuAlgorithmName) {
      algorithm_compliance_descriptor = CreateCorotatedCpuAlgorithmComplianceDescriptor(
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
      algorithm_compliance_descriptor);

    auto start_time = std::chrono::steady_clock::now();
    auto last_frame_time = start_time;
    while (window_agent.Tick()) {
      auto now = std::chrono::steady_clock::now();
      float frame_dt = std::chrono::duration<float>(now - last_frame_time).count();
      if (frame_dt < 0.0f) frame_dt = 0.0f;
      if (frame_dt > 0.05f) frame_dt = 0.05f;
      last_frame_time = now;
      SceneViewFrameState scene_view_frame = scene_view_agent.Tick(render_agent.scene_view_bounds(), window_agent);
      phys_agent.SetRunState(render_agent.phys_run_state());

      phys_agent.Tick(mesh, scene_view_agent.viewport(), scene_view_agent.camera(), window_agent.input(), scene_view_frame.mouse_pixel, frame_dt);
      RenderUiState ui = app_orchestration::AppFrameSyncGlue::BuildRenderUiStateFromRenderAgentAndPhysAgent(
        render_agent,
        phys_agent,
        std::chrono::duration<float>(std::chrono::steady_clock::now() - start_time).count());

      RenderFrameResult frame = render_agent.Tick(mesh, scene_view_agent.camera(), ui);
      app_orchestration::AppFrameSyncGlue::ApplyRenderFrameResultOnPhysAgentAndMesh(frame, &phys_agent, &mesh);
    }
    phys_agent.Destroy();
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
