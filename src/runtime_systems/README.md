# Runtime Systems

This layer owns the runtime shell around SDL, ImGui, and Vulkan.

## Public Interface

- `runtime_systems/runtime_systems.h` is the only public facade for this layer
- `RuntimeEnvironment`
- runtime job entry points are exposed through the facade for upper layers that need them
- GPU job execution lives in the `gpu_job_system.cpp` implementation and is exposed through the job facade.

## Internal Details

- `SdlWindow` owns the SDL window object and input snapshot.
- `ImGuiVulkanRuntime` owns the ImGui, SDL backend, and Vulkan rendering lifecycle.
- `MemoryManager` owns the tracked default `std::pmr::memory_resource` used by buffer-building code.
- `job_system.h`, `gpu_job_system.h`, `runtime_environment.h`, and the rendering/window implementation headers are internal implementation headers.
- Upper layers should only depend on the public runtime-systems interfaces listed above.

## Coordinate Convention

- Runtime rendering uses a left-handed coordinate system.
- The origin `[0,0,0]` is at the lower-left near corner.
- `+X` points right, `+Y` points up, and `+Z` points into the screen.
- Vulkan viewport setup flips framebuffer coordinates to preserve this convention.
- Render Preview shaders consume preview-page pixel coordinates.
- In Render Preview, `[0,0]` maps to the lower-left corner of the content region.
