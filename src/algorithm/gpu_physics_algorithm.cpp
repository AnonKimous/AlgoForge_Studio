#include "gpu_physics_algorithm.h"

#include <spirv_reflect.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace algorithm {

namespace {

constexpr VkFormat kConvolutionImageFormat = VK_FORMAT_R32_SFLOAT;
constexpr uint32_t kInvalidBinding = std::numeric_limits<uint32_t>::max();

struct _ConvolutionPushConstants {
  uint32_t width{};
  uint32_t height{};
  uint32_t mask[9]{};
  float kernel[9]{};
};

struct _BufferResource {
  VkBuffer buffer{};
  VmaAllocation allocation{};
  VmaAllocationInfo allocation_info{};
  VkDeviceSize size{};
};

struct _ImageResource {
  VkImage image{};
  VmaAllocation allocation{};
  VkImageView view{};
  uint32_t width{};
  uint32_t height{};
};

struct _ReflectedComputeShader {
  std::vector<char> code;
  std::vector<VkDescriptorSetLayoutBinding> descriptor_bindings;
  uint32_t input_binding{kInvalidBinding};
  uint32_t output_binding{kInvalidBinding};
  VkPushConstantRange push_constant_range{};
  bool has_push_constants{false};
};

std::vector<char> _ReadBinaryFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open compute shader: " + path);
  }
  return std::vector<char>((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool _SupportsGpuAlgorithm(const std::string& algorithm_name) {
  return algorithm_name == "physics_convolution_demo" || algorithm_name == "convolution_demo";
}

VmaAllocator _CreateAllocator(const VulkanComputeContextView& context) {
  VmaAllocatorCreateInfo info{};
  info.instance = context.instance;
  info.physicalDevice = context.physical_device;
  info.device = context.device;
  info.vulkanApiVersion = VK_API_VERSION_1_3;
  VmaAllocator allocator{};
  if (vmaCreateAllocator(&info, &allocator) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create GPU algorithm VMA allocator");
  }
  return allocator;
}

_BufferResource _CreateBuffer(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage, VmaAllocationCreateFlags flags) {
  _BufferResource buffer{};
  VkBufferCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = memory_usage;
  alloc_info.flags = flags;
  if (vmaCreateBuffer(allocator, &info, &alloc_info, &buffer.buffer, &buffer.allocation, &buffer.allocation_info) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create GPU algorithm buffer");
  }
  buffer.size = size;
  return buffer;
}

void _DestroyBuffer(VmaAllocator allocator, _BufferResource* buffer) {
  if (!buffer) return;
  if (buffer->buffer) {
    vmaDestroyBuffer(allocator, buffer->buffer, buffer->allocation);
  }
  *buffer = {};
}

_ImageResource _CreateStorageImage(VmaAllocator allocator, uint32_t width, uint32_t height) {
  _ImageResource image{};
  VkImageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = kConvolutionImageFormat;
  info.extent = VkExtent3D{width, height, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VmaAllocationCreateInfo alloc_info{};
  alloc_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (vmaCreateImage(allocator, &info, &alloc_info, &image.image, &image.allocation, nullptr) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create GPU algorithm image");
  }

  image.width = width;
  image.height = height;
  return image;
}

void _DestroyImage(VmaAllocator allocator, VkDevice device, _ImageResource* image) {
  if (!image) return;
  if (image->view) vkDestroyImageView(device, image->view, nullptr);
  if (image->image) vmaDestroyImage(allocator, image->image, image->allocation);
  *image = {};
}

VkShaderModule _CreateShaderModule(VkDevice device, const std::vector<char>& code, const std::string& path) {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = code.size();
  info.pCode = reinterpret_cast<const uint32_t*>(code.data());
  VkShaderModule shader_module{};
  if (vkCreateShaderModule(device, &info, nullptr, &shader_module) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create GPU algorithm shader module: " + path);
  }
  return shader_module;
}

_ReflectedComputeShader _ReflectComputeShader(const std::string& path) {
  _ReflectedComputeShader reflected{};
  reflected.code = _ReadBinaryFile(path);

  SpvReflectShaderModule module{};
  SpvReflectResult reflect_result = spvReflectCreateShaderModule(reflected.code.size(), reflected.code.data(), &module);
  if (reflect_result != SPV_REFLECT_RESULT_SUCCESS) {
    throw std::runtime_error("Failed to reflect GPU algorithm shader: " + path);
  }

  uint32_t binding_count = 0;
  spvReflectEnumerateDescriptorBindings(&module, &binding_count, nullptr);
  std::vector<SpvReflectDescriptorBinding*> bindings(binding_count);
  spvReflectEnumerateDescriptorBindings(&module, &binding_count, bindings.data());

  for (SpvReflectDescriptorBinding* binding : bindings) {
    if (!binding || binding->set != 0) continue;
    VkDescriptorSetLayoutBinding layout_binding{};
    layout_binding.binding = binding->binding;
    layout_binding.descriptorType = static_cast<VkDescriptorType>(binding->descriptor_type);
    layout_binding.descriptorCount = binding->count == 0 ? 1u : binding->count;
    layout_binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    reflected.descriptor_bindings.push_back(layout_binding);

    const std::string resource_name = binding->name ? binding->name : "";
    if (resource_name == "input_image") {
      reflected.input_binding = binding->binding;
    } else if (resource_name == "output_image") {
      reflected.output_binding = binding->binding;
    }
  }

  std::sort(
    reflected.descriptor_bindings.begin(),
    reflected.descriptor_bindings.end(),
    [](const VkDescriptorSetLayoutBinding& a, const VkDescriptorSetLayoutBinding& b) {
      return a.binding < b.binding;
    });

  uint32_t push_constant_count = 0;
  spvReflectEnumeratePushConstantBlocks(&module, &push_constant_count, nullptr);
  if (push_constant_count > 0) {
    std::vector<SpvReflectBlockVariable*> push_constants(push_constant_count);
    spvReflectEnumeratePushConstantBlocks(&module, &push_constant_count, push_constants.data());
    if (push_constants[0]) {
      reflected.push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
      reflected.push_constant_range.offset = push_constants[0]->offset;
      reflected.push_constant_range.size = push_constants[0]->size;
      reflected.has_push_constants = push_constants[0]->size > 0;
    }
  }

  spvReflectDestroyShaderModule(&module);
  return reflected;
}

void _TransitionImageLayout(VkCommandBuffer command_buffer, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = old_layout;
  barrier.newLayout = new_layout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;

  VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

  if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_GENERAL) {
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    dst_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (old_layout == VK_IMAGE_LAYOUT_GENERAL && new_layout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    src_stage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else {
    throw std::runtime_error("Unsupported GPU algorithm image layout transition");
  }

  vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool _RunConvolutionDemo(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  const VulkanComputeContextView& context = request.compute_context;
  if (!context.valid || !context.instance || !context.device || !context.physical_device || !context.queue) {
    return false;
  }
  if (request.config.gpu_shader.shader_name.empty()) {
    return false;
  }

  const _ReflectedComputeShader reflected = _ReflectComputeShader(request.config.gpu_shader.shader_name);
  if (reflected.input_binding == kInvalidBinding || reflected.output_binding == kInvalidBinding || reflected.descriptor_bindings.empty()) {
    return false;
  }

  const uint32_t width = 4;
  const uint32_t height = 4;
  const VkDeviceSize element_count = static_cast<VkDeviceSize>(width) * static_cast<VkDeviceSize>(height);
  const VkDeviceSize byte_count = element_count * sizeof(float);

  VmaAllocator allocator = _CreateAllocator(context);
  VkCommandPool command_pool{};
  _BufferResource upload_buffer{};
  _BufferResource readback_buffer{};
  _ImageResource input_image{};
  _ImageResource output_image{};
  VkDescriptorSetLayout descriptor_set_layout{};
  VkDescriptorPool descriptor_pool{};
  VkPipelineLayout pipeline_layout{};
  VkShaderModule shader_module{};
  VkPipeline pipeline{};

  try {
    VkCommandPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool_info.queueFamilyIndex = context.queue_family_index;
    if (vkCreateCommandPool(context.device, &pool_info, nullptr, &command_pool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm command pool");
    }

    upload_buffer = _CreateBuffer(
      allocator,
      byte_count,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    readback_buffer = _CreateBuffer(
      allocator,
      byte_count,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VMA_MEMORY_USAGE_AUTO_PREFER_HOST,
      VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
    input_image = _CreateStorageImage(allocator, width, height);
    output_image = _CreateStorageImage(allocator, width, height);

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = kConvolutionImageFormat;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    view_info.image = input_image.image;
    if (vkCreateImageView(context.device, &view_info, nullptr, &input_image.view) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm input image view");
    }
    view_info.image = output_image.image;
    if (vkCreateImageView(context.device, &view_info, nullptr, &output_image.view) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm output image view");
    }

    std::vector<float> upload_values(static_cast<size_t>(element_count), 1.0f);
    const size_t upload_count = std::min(upload_values.size(), request.config.gpu_shader.shader_data.size());
    for (size_t i = 0; i < upload_count; ++i) {
      upload_values[i] = request.config.gpu_shader.shader_data[i];
    }
    if (!upload_buffer.allocation_info.pMappedData || !readback_buffer.allocation_info.pMappedData) {
      throw std::runtime_error("GPU algorithm mapped allocation was not available");
    }
    std::memcpy(upload_buffer.allocation_info.pMappedData, upload_values.data(), static_cast<size_t>(byte_count));

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(reflected.descriptor_bindings.size());
    layout_info.pBindings = reflected.descriptor_bindings.data();
    if (vkCreateDescriptorSetLayout(context.device, &layout_info, nullptr, &descriptor_set_layout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm descriptor set layout");
    }

    std::vector<VkDescriptorPoolSize> pool_sizes;
    pool_sizes.reserve(reflected.descriptor_bindings.size());
    for (const VkDescriptorSetLayoutBinding& binding : reflected.descriptor_bindings) {
      pool_sizes.push_back(VkDescriptorPoolSize{binding.descriptorType, binding.descriptorCount});
    }

    VkDescriptorPoolCreateInfo descriptor_pool_info{};
    descriptor_pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptor_pool_info.maxSets = 1;
    descriptor_pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
    descriptor_pool_info.pPoolSizes = pool_sizes.data();
    if (vkCreateDescriptorPool(context.device, &descriptor_pool_info, nullptr, &descriptor_pool) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm descriptor pool");
    }

    VkDescriptorSet descriptor_set{};
    VkDescriptorSetAllocateInfo descriptor_alloc{};
    descriptor_alloc.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptor_alloc.descriptorPool = descriptor_pool;
    descriptor_alloc.descriptorSetCount = 1;
    descriptor_alloc.pSetLayouts = &descriptor_set_layout;
    if (vkAllocateDescriptorSets(context.device, &descriptor_alloc, &descriptor_set) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate GPU algorithm descriptor set");
    }

    VkDescriptorImageInfo input_descriptor{};
    input_descriptor.imageView = input_image.view;
    input_descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    VkDescriptorImageInfo output_descriptor{};
    output_descriptor.imageView = output_image.view;
    output_descriptor.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptor_set;
    writes[0].dstBinding = reflected.input_binding;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].pImageInfo = &input_descriptor;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptor_set;
    writes[1].dstBinding = reflected.output_binding;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].pImageInfo = &output_descriptor;
    vkUpdateDescriptorSets(context.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &descriptor_set_layout;
    if (reflected.has_push_constants) {
      pipeline_layout_info.pushConstantRangeCount = 1;
      pipeline_layout_info.pPushConstantRanges = &reflected.push_constant_range;
    }
    if (vkCreatePipelineLayout(context.device, &pipeline_layout_info, nullptr, &pipeline_layout) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm pipeline layout");
    }

    shader_module = _CreateShaderModule(context.device, reflected.code, request.config.gpu_shader.shader_name);
    VkPipelineShaderStageCreateInfo stage_info{};
    stage_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage_info.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stage_info.module = shader_module;
    stage_info.pName = "main";

    VkComputePipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline_info.stage = stage_info;
    pipeline_info.layout = pipeline_layout;
    if (vkCreateComputePipelines(context.device, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create GPU algorithm compute pipeline");
    }

    VkCommandBufferAllocateInfo command_alloc{};
    command_alloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command_alloc.commandPool = command_pool;
    command_alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    command_alloc.commandBufferCount = 1;
    VkCommandBuffer command_buffer{};
    if (vkAllocateCommandBuffers(context.device, &command_alloc, &command_buffer) != VK_SUCCESS) {
      throw std::runtime_error("Failed to allocate GPU algorithm command buffer");
    }

    VkCommandBufferBeginInfo begin_info{};
    begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
      throw std::runtime_error("Failed to begin GPU algorithm command buffer");
    }

    _TransitionImageLayout(command_buffer, input_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    _TransitionImageLayout(command_buffer, output_image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL);

    VkBufferImageCopy upload_region{};
    upload_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    upload_region.imageSubresource.layerCount = 1;
    upload_region.imageExtent = VkExtent3D{width, height, 1};
    vkCmdCopyBufferToImage(command_buffer, upload_buffer.buffer, input_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &upload_region);
    _TransitionImageLayout(command_buffer, input_image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL);

    vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);

    if (reflected.has_push_constants) {
      _ConvolutionPushConstants push_constants{};
      push_constants.width = width;
      push_constants.height = height;
      for (int i = 0; i < 9; ++i) {
        push_constants.mask[i] = i < static_cast<int>(request.config.gpu_shader.shader_mask.size()) ? request.config.gpu_shader.shader_mask[static_cast<size_t>(i)] : 1u;
        push_constants.kernel[i] = i < static_cast<int>(request.config.gpu_shader.shader_data.size()) ? request.config.gpu_shader.shader_data[static_cast<size_t>(i)] : 1.0f;
      }
      vkCmdPushConstants(command_buffer, pipeline_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(_ConvolutionPushConstants), &push_constants);
    }

    vkCmdDispatch(command_buffer, (width + 7u) / 8u, (height + 7u) / 8u, 1);

    VkImageMemoryBarrier compute_to_copy{};
    compute_to_copy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    compute_to_copy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    compute_to_copy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    compute_to_copy.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    compute_to_copy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    compute_to_copy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    compute_to_copy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    compute_to_copy.image = output_image.image;
    compute_to_copy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    compute_to_copy.subresourceRange.levelCount = 1;
    compute_to_copy.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(
      command_buffer,
      VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
      VK_PIPELINE_STAGE_TRANSFER_BIT,
      0,
      0,
      nullptr,
      0,
      nullptr,
      1,
      &compute_to_copy);

    VkBufferImageCopy readback_region{};
    readback_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    readback_region.imageSubresource.layerCount = 1;
    readback_region.imageExtent = VkExtent3D{width, height, 1};
    vkCmdCopyImageToBuffer(command_buffer, output_image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buffer.buffer, 1, &readback_region);

    if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
      throw std::runtime_error("Failed to end GPU algorithm command buffer");
    }

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer;
    if (vkQueueSubmit(context.queue, 1, &submit, VK_NULL_HANDLE) != VK_SUCCESS) {
      throw std::runtime_error("Failed to submit GPU algorithm command buffer");
    }
    if (vkQueueWaitIdle(context.queue) != VK_SUCCESS) {
      throw std::runtime_error("Failed to wait for GPU algorithm queue");
    }

    std::vector<float> result_values(static_cast<size_t>(element_count), 0.0f);
    std::memcpy(result_values.data(), readback_buffer.allocation_info.pMappedData, static_cast<size_t>(byte_count));

    result->executed = true;
    result->step_output.positions = request.input.positions;
    result->step_output.total_velocities = request.input.total_velocities;
    result->step_output.linear_velocities = request.input.linear_velocities;
    result->step_output.angular_velocities = request.input.angular_velocities;
    result->step_output.displacement_interventions = request.input.displacement_interventions;
    result->step_output.velocity_interventions = request.input.velocity_interventions;
    result->step_output.force_interventions = request.input.force_interventions;
    result->gpu_dispatch_debug.shader_name = request.config.gpu_shader.shader_name;
    result->gpu_dispatch_debug.width = width;
    result->gpu_dispatch_debug.height = height;
    result->gpu_dispatch_debug.values = std::move(result_values);
    result->gpu_dispatch_debug.valid = true;

    vkFreeCommandBuffers(context.device, command_pool, 1, &command_buffer);
  } catch (...) {
    if (pipeline) vkDestroyPipeline(context.device, pipeline, nullptr);
    if (shader_module) vkDestroyShaderModule(context.device, shader_module, nullptr);
    if (pipeline_layout) vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
    if (descriptor_pool) vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
    if (descriptor_set_layout) vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
    _DestroyImage(allocator, context.device, &output_image);
    _DestroyImage(allocator, context.device, &input_image);
    _DestroyBuffer(allocator, &readback_buffer);
    _DestroyBuffer(allocator, &upload_buffer);
    if (command_pool) vkDestroyCommandPool(context.device, command_pool, nullptr);
    vmaDestroyAllocator(allocator);
    throw;
  }

  if (pipeline) vkDestroyPipeline(context.device, pipeline, nullptr);
  if (shader_module) vkDestroyShaderModule(context.device, shader_module, nullptr);
  if (pipeline_layout) vkDestroyPipelineLayout(context.device, pipeline_layout, nullptr);
  if (descriptor_pool) vkDestroyDescriptorPool(context.device, descriptor_pool, nullptr);
  if (descriptor_set_layout) vkDestroyDescriptorSetLayout(context.device, descriptor_set_layout, nullptr);
  _DestroyImage(allocator, context.device, &output_image);
  _DestroyImage(allocator, context.device, &input_image);
  _DestroyBuffer(allocator, &readback_buffer);
  _DestroyBuffer(allocator, &upload_buffer);
  if (command_pool) vkDestroyCommandPool(context.device, command_pool, nullptr);
  vmaDestroyAllocator(allocator);
  return true;
}

}  // namespace

bool GpuPhysicsAlgorithm_Run(const PhysicsAlgorithmRequest& request, PhysicsAlgorithmResult* result) {
  if (!result) return false;
  if (!_SupportsGpuAlgorithm(request.config.algorithm_name)) {
    return false;
  }
  return _RunConvolutionDemo(request, result);
}

}  // namespace algorithm

