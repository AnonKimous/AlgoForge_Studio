# Runtime Systems

This layer owns the runtime shell around SDL, ImGui, and Vulkan.

## Public Interface

- `RuntimeEnvironment`
- `MemoryManager`

## Internal Details

- `SdlWindow` owns the SDL window object and input snapshot.
- `ImGuiVulkanRuntime` owns the ImGui, SDL backend, and Vulkan rendering lifecycle.
- `MemoryManager` owns the tracked default `std::pmr::memory_resource` used by buffer-building code.
- Upper layers should only depend on the public runtime-systems interfaces listed above.
