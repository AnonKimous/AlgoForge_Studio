from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
runtime = ROOT / "src/runtimesys/render/imgui_vulkan_runtime.cpp"
text = runtime.read_text(encoding="utf-8")

selection_begin = "  for (VkPhysicalDevice device : physical_devices) {\n"
selection_end = "  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {\n"
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
text = text[:begin] + selection + text[end:]

old_error = '''  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {
    throw std::runtime_error("No suitable Vulkan device queue family found for ImGui");
  }
'''
new_error = '''  if (physical_device_ == VK_NULL_HANDLE || queue_family_ == UINT32_MAX) {
    throw std::runtime_error(
      "No Vulkan queue family supports both graphics and presentation.");
  }
'''
if text.count(old_error) != 1:
    raise RuntimeError("Vulkan queue-selection error block was not found exactly once")
text = text.replace(old_error, new_error, 1)

old_count = '''  const uint32_t queue_count = selected_queue_families[queue_family_].queueCount;
  if (queue_count < 2u) {
    throw std::runtime_error("The Vulkan present queue family must expose a second queue for algorithms");
  }
'''
new_count = '''  const uint32_t queue_count = selected_queue_families[queue_family_].queueCount;
'''
if text.count(old_count) != 1:
    raise RuntimeError("Vulkan two-queue requirement block was not found exactly once")
text = text.replace(old_count, new_count, 1)

old_algorithm_queues = '''  algorithm_queues_.clear();
  algorithm_queues_.reserve(queue_count - 1u);
  for (uint32_t queue_index = 1u; queue_index < queue_count; ++queue_index) {
    VkQueue algorithm_queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device_, queue_family_, queue_index, &algorithm_queue);
    algorithm_queues_.push_back(algorithm_queue);
  }
'''
new_algorithm_queues = '''  algorithm_queues_.clear();
  uint32_t first_algorithm_queue_index = 0u;
  if (queue_count > 1u) {
    first_algorithm_queue_index = 1u;
    algorithm_queues_.reserve(queue_count - 1u);
    for (uint32_t queue_index = 1u; queue_index < queue_count; ++queue_index) {
      VkQueue algorithm_queue = VK_NULL_HANDLE;
      vkGetDeviceQueue(device_, queue_family_, queue_index, &algorithm_queue);
      algorithm_queues_.push_back(algorithm_queue);
    }
  } else {
    algorithm_queues_.push_back(queue_);
  }
'''
if text.count(old_algorithm_queues) != 1:
    raise RuntimeError("Vulkan algorithm queue initialization block was not found exactly once")
text = text.replace(old_algorithm_queues, new_algorithm_queues, 1)

old_registry_call = '''  RuntimeVkContextRegistry::Instance().SetAlgorithmQueues(algorithm_queues_);
'''
new_registry_call = '''  RuntimeVkContextRegistry::Instance().SetAlgorithmQueues(
    algorithm_queues_,
    first_algorithm_queue_index);
'''
if text.count(old_registry_call) != 1:
    raise RuntimeError("RuntimeVkContextRegistry queue registration was not found exactly once")
text = text.replace(old_registry_call, new_registry_call, 1)

runtime.write_text(text, encoding="utf-8")
