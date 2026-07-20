from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def replace_once(path: Path, old: str, new: str) -> None:
    text = path.read_text(encoding="utf-8")
    if text.count(old) != 1:
        raise RuntimeError(f"expected one match in {path}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


header = ROOT / "src/runtimesys/runtime_gpu_context.h"
replace_once(
    header,
    "  void SetAlgorithmQueues(std::vector<VkQueue> queues);\n",
    "  void SetAlgorithmQueues(\n"
    "    std::vector<VkQueue> queues,\n"
    "    uint32_t first_queue_index);\n",
)
replace_once(
    header,
    "  std::vector<VkQueue> algorithm_queues_{};\n"
    "  mutable std::unordered_map<const void*, uint32_t> queue_assignments_{};\n",
    "  std::vector<VkQueue> algorithm_queues_{};\n"
    "  uint32_t algorithm_queue_first_index_{1u};\n"
    "  mutable std::unordered_map<const void*, uint32_t> queue_assignments_{};\n",
)

registry = ROOT / "src/runtimesys/algorithm_gpu_context.cpp"
replace_once(
    registry,
    "  algorithm_queues_.clear();\n"
    "  queue_assignments_.clear();\n",
    "  algorithm_queues_.clear();\n"
    "  algorithm_queue_first_index_ = 1u;\n"
    "  queue_assignments_.clear();\n",
)
replace_once(
    registry,
    "void RuntimeVkContextRegistry::SetAlgorithmQueues(std::vector<VkQueue> queues) {\n"
    "  std::lock_guard<std::mutex> lock(mutex_);\n"
    "  algorithm_queues_ = std::move(queues);\n",
    "void RuntimeVkContextRegistry::SetAlgorithmQueues(\n"
    "  std::vector<VkQueue> queues,\n"
    "  uint32_t first_queue_index) {\n"
    "  std::lock_guard<std::mutex> lock(mutex_);\n"
    "  algorithm_queues_ = std::move(queues);\n"
    "  algorithm_queue_first_index_ = first_queue_index;\n",
)
replace_once(
    registry,
    "  result.queue = algorithm_queues_[assignment->second];\n"
    "  result.queue_index = assignment->second + 1u;\n",
    "  result.queue = algorithm_queues_[assignment->second];\n"
    "  result.queue_index = algorithm_queue_first_index_ + assignment->second;\n",
)
# Reset the first index in Clear as well. The first identical clear block was
# already changed in Set(), so this targets the remaining block.
replace_once(
    registry,
    "  context_ = {};\n"
    "  algorithm_queues_.clear();\n"
    "  queue_assignments_.clear();\n",
    "  context_ = {};\n"
    "  algorithm_queues_.clear();\n"
    "  algorithm_queue_first_index_ = 1u;\n"
    "  queue_assignments_.clear();\n",
)

runtime = ROOT / "src/runtimesys/render/imgui_vulkan_runtime.cpp"
selection_begin = "  for (VkPhysicalDevice device : physical_devices) {\n"
selection_end = "  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {\n"
text = runtime.read_text(encoding="utf-8")
begin = text.index(selection_begin)
end = text.index(selection_end, begin)
selection = '''  bool selected_multiple_queues = false;
  for (VkPhysicalDevice device : physical_devices) {
    uint32_t queue_family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_family_count, queue_families.data());

    for (uint32_t family = 0; family < queue_family_count; ++family) {
      VkBool32 present_supported = VK_FALSE;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, family, surface_, &present_supported);
      const bool graphics_supported =
        (queue_families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
      if (!graphics_supported || !present_supported || queue_families[family].queueCount == 0u) {
        continue;
      }

      const bool has_multiple_queues = queue_families[family].queueCount >= 2u;
      if (physical_device_ == VK_NULL_HANDLE || has_multiple_queues) {
        physical_device_ = device;
        queue_family_ = family;
        selected_multiple_queues = has_multiple_queues;
      }
      if (selected_multiple_queues) {
        break;
      }
    }

    if (selected_multiple_queues) {
      break;
    }
  }

'''
runtime.write_text(text[:begin] + selection + text[end:], encoding="utf-8")
replace_once(
    runtime,
    "  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {\n"
    "    throw std::runtime_error(\"No suitable Vulkan device queue family found for ImGui\");\n"
    "  }\n",
    "  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {\n"
    "    throw std::runtime_error(\n"
    "      \"No Vulkan queue family supports both graphics and presentation.\");\n"
    "  }\n",
)
replace_once(
    runtime,
    "  const uint32_t queue_count = selected_queue_families[queue_family_].queueCount;\n"
    "  if (queue_count < 2u) {\n"
    "    throw std::runtime_error(\"The Vulkan present queue family must expose a second queue for algorithms\");\n"
    "  }\n",
    "  const uint32_t queue_count = selected_queue_families[queue_family_].queueCount;\n",
)
replace_once(
    runtime,
    "  algorithm_queues_.clear();\n"
    "  algorithm_queues_.reserve(queue_count - 1u);\n"
    "  for (uint32_t queue_index = 1u; queue_index < queue_count; ++queue_index) {\n"
    "    VkQueue algorithm_queue = VK_NULL_HANDLE;\n"
    "    vkGetDeviceQueue(device_, queue_family_, queue_index, &algorithm_queue);\n"
    "    algorithm_queues_.push_back(algorithm_queue);\n"
    "  }\n",
    "  algorithm_queues_.clear();\n"
    "  uint32_t first_algorithm_queue_index = 0u;\n"
    "  if (queue_count > 1u) {\n"
    "    first_algorithm_queue_index = 1u;\n"
    "    algorithm_queues_.reserve(queue_count - 1u);\n"
    "    for (uint32_t queue_index = 1u; queue_index < queue_count; ++queue_index) {\n"
    "      VkQueue algorithm_queue = VK_NULL_HANDLE;\n"
    "      vkGetDeviceQueue(device_, queue_family_, queue_index, &algorithm_queue);\n"
    "      algorithm_queues_.push_back(algorithm_queue);\n"
    "    }\n"
    "  } else {\n"
    "    algorithm_queues_.push_back(queue_);\n"
    "  }\n",
)
replace_once(
    runtime,
    "  RuntimeVkContextRegistry::Instance().SetAlgorithmQueues(algorithm_queues_);\n",
    "  RuntimeVkContextRegistry::Instance().SetAlgorithmQueues(\n"
    "    algorithm_queues_,\n"
    "    first_algorithm_queue_index);\n",
)
