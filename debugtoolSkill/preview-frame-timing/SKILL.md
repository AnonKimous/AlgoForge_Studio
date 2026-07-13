---
name: preview-frame-timing
description: Diagnose Preview frame gaps by correlating backend ticks, agent execution, Jobs/Vulkan work, result texture updates, ImGui submission, and presentation.
---

# Preview Frame Timing

## Frame lifetime

```text
DebugToolBackendRuntime::Tick
  -> AgentManager::Tick
     -> algorithm or pipeline stages
        -> Jobs or Vulkan execution
  -> RuntimeEnvironment::Tick
     -> ImGuiVulkanRuntime::Tick
        -> apply preview target extent
        -> refresh the result texture descriptor
        -> build ImGui draw data
        -> acquire a swapchain image
        -> wait for the ImGui frame fence
        -> record and submit the ImGui command buffer
        -> present the swapchain image
```

The result is produced before ImGui submission. If Agent Tick is blocked, Preview keeps showing the last submitted image and cannot present a new result.

## Log markers

The GUI log is normally `testData\debugTool_gui.log`.

- `preview_backend_frame.begin/end`: complete backend-frame lifetime and the interval from the previous backend-frame start.
- `backend_tick.agent_done`: AgentManager completed. If absent, inspect `jobs_exec`, `scheduler_submit`, and `stage_plan` markers.
- `preview.frame.begin/end`: complete ImGui Preview-frame lifetime and the interval from the previous completed Preview frame.
- `preview.result_texture.refresh.begin/end`: result-image lookup and descriptor replacement. A missing `end` identifies a result-texture stall.
- `preview.vk.acquire.done`: swapchain image acquisition.
- `preview.vk.fence_wait.done`: wait for the ImGui frame fence.
- `preview.vk.queue_submit.done`: ImGui command-buffer submission.
- `preview.vk.present.done`: presentation to the swapchain.

## Reading a four-second gap

Use `preview.frame.begin` and the next matching `preview.frame.end`. The `inter_frame_seconds` value is the actual wall-clock interval from the previous completed Preview frame to the current Preview frame. Do not infer it from a completed tick's elapsed time.

If `preview.frame.begin` exists without `preview.frame.end`, the current frame is blocked. The last emitted marker identifies the blocking phase. If there is no new `preview.frame.begin`, the block is before ImGui Preview starts, usually inside Agent Tick.

Useful PowerShell command:

```powershell
Select-String testData\debugTool_gui.log -Pattern 'preview.frame.begin|preview.frame.end|preview.vk|preview.result_texture|backend_tick|jobs_exec|stage_plan' | Select-Object -Last 160
```

## Jobs and Vulkan diagnosis

- `jobs_exec.submit_blocking.begin` without its `end` means the Jobs path is blocked.
- `scheduler_submit.begin` without `scheduler_submit.end` means scheduler or stage execution is blocked.
- A long `preview.vk.fence_wait.done` interval means the ImGui queue is waiting for an earlier submission.
- A long acquire or present interval means swapchain pacing or queue synchronization is blocked.
- A missing `preview.result_texture.refresh.end` points to result-image or ImGui descriptor management, including `ImGui_ImplVulkan_AddTexture`.

Completed backend totals do not describe an incomplete frame. Incomplete frames require begin/end correlation and the last phase marker.
