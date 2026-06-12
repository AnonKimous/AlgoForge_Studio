#include "preview_renderer.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <imgui_impl_vulkan.h>

namespace runtime_systems {

namespace {

std::string MakeVkErrorMessage(const char* prefix, VkResult err) {
  return std::string(prefix) + " failed: VkResult=" + std::to_string(static_cast<int>(err));
}

std::vector<VkDynamicState> DynamicStates() {
  return {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
}

}  // namespace

void PreviewRenderer::CheckVkResult(VkResult err) {
  if (err != VK_SUCCESS) {
    throw std::runtime_error(MakeVkErrorMessage("Vulkan call", err));
  }
}

std::string PreviewRenderer::VkErrorMessage(const char* prefix, VkResult err) {
  return MakeVkErrorMessage(prefix, err);
}

std::vector<std::byte> PreviewRenderer::ReadBinaryFile(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  file.seekg(0, std::ios::end);
  const std::streamoff end = file.tellg();
  if (end <= 0) {
    return {};
  }
  std::vector<std::byte> bytes(static_cast<size_t>(end));
  file.seekg(0, std::ios::beg);
  file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  if (!file) {
    return {};
  }
  return bytes;
}

std::string PreviewRenderer::ResolveShaderBinaryPath(const std::string& path) {
  if (path.empty()) {
    return {};
  }

  const std::filesystem::path source_path(path);
  if (source_path.string().ends_with(".spv")) {
    return source_path.string();
  }

  return std::filesystem::path(source_path.string() + ".spv").string();
}

bool PreviewRenderer::Init(
  VkInstance instance,
  VkPhysicalDevice physical_device,
  VkDevice device,
  VkDescriptorPool descriptor_pool,
  VmaAllocator allocator) {
  instance_ = instance;
  physical_device_ = physical_device;
  device_ = device;
  descriptor_pool_ = descriptor_pool;
  allocator_ = allocator;
  return true;
}

void PreviewRenderer::SetTargetExtent(VkExtent2D extent) {
  if (target_.extent.width == extent.width && target_.extent.height == extent.height) {
    return;
  }
  target_.extent = extent;
  DestroyTarget();
}

void PreviewRenderer::SetRequest(RenderPreviewRequest request) {
  request_ = std::move(request);
  if (!request_.valid) {
    DestroyPipeline();
    DestroyTarget();
  }
}

bool PreviewRenderer::HasTexture() const {
  return target_.descriptor_set != VK_NULL_HANDLE;
}

VkDescriptorSet PreviewRenderer::PreviewTextureDescriptorSet() const {
  return target_.descriptor_set;
}

VkExtent2D PreviewRenderer::PreviewTextureExtent() const {
  return target_.extent;
}

bool PreviewRenderer::UploadBuffer(
  PreviewBufferResource* buffer_resource,
  const RenderPreviewBuffer& source_buffer) {
  if (!buffer_resource) {
    return false;
  }

  const VkDeviceSize required_size = static_cast<VkDeviceSize>(source_buffer.bytes.size());
  if (buffer_resource->buffer == VK_NULL_HANDLE || buffer_resource->size_bytes < required_size) {
    if (buffer_resource->buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(allocator_, buffer_resource->buffer, buffer_resource->allocation);
      buffer_resource->buffer = VK_NULL_HANDLE;
      buffer_resource->allocation = VK_NULL_HANDLE;
      buffer_resource->allocation_info = {};
      buffer_resource->size_bytes = 0u;
    }

    VkBufferCreateInfo buffer_info{};
    buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    buffer_info.size = std::max<VkDeviceSize>(required_size, 1u);
    buffer_info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO;
    allocation_info.flags =
      VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
      VMA_ALLOCATION_CREATE_MAPPED_BIT;

    CheckVkResult(vmaCreateBuffer(
      allocator_,
      &buffer_info,
      &allocation_info,
      &buffer_resource->buffer,
      &buffer_resource->allocation,
      &buffer_resource->allocation_info));
    buffer_resource->size_bytes = buffer_info.size;
  }

  if (required_size > 0u && !source_buffer.bytes.empty()) {
    std::memcpy(
      buffer_resource->allocation_info.pMappedData,
      source_buffer.bytes.data(),
      source_buffer.bytes.size());
    CheckVkResult(vmaFlushAllocation(
      allocator_,
      buffer_resource->allocation,
      0u,
      buffer_resource->size_bytes));
  }

  return true;
}

bool PreviewRenderer::CreateTarget() {
  if (target_.descriptor_set != VK_NULL_HANDLE) {
    return true;
  }
  if (device_ == VK_NULL_HANDLE || allocator_ == VK_NULL_HANDLE || descriptor_pool_ == VK_NULL_HANDLE) {
    return false;
  }

  try {
    VkImageCreateInfo image_info{};
    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.format = target_.format;
    image_info.extent = {target_.extent.width, target_.extent.height, 1u};
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocation_info{};
    allocation_info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    CheckVkResult(vmaCreateImage(
      allocator_,
      &image_info,
      &allocation_info,
      &target_.image,
      &target_.allocation,
      nullptr));

    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = target_.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = target_.format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    CheckVkResult(vkCreateImageView(device_, &view_info, nullptr, &target_.view));

    VkAttachmentDescription attachment{};
    attachment.format = target_.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference color_attachment{};
    color_attachment.attachment = 0;
    color_attachment.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_attachment;

    VkRenderPassCreateInfo render_pass_info{};
    render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    render_pass_info.attachmentCount = 1;
    render_pass_info.pAttachments = &attachment;
    render_pass_info.subpassCount = 1;
    render_pass_info.pSubpasses = &subpass;
    CheckVkResult(vkCreateRenderPass(device_, &render_pass_info, nullptr, &target_.render_pass));

    VkFramebufferCreateInfo framebuffer_info{};
    framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebuffer_info.renderPass = target_.render_pass;
    framebuffer_info.attachmentCount = 1;
    framebuffer_info.pAttachments = &target_.view;
    framebuffer_info.width = target_.extent.width;
    framebuffer_info.height = target_.extent.height;
    framebuffer_info.layers = 1;
    CheckVkResult(vkCreateFramebuffer(device_, &framebuffer_info, nullptr, &target_.framebuffer));

    target_.descriptor_set = ImGui_ImplVulkan_AddTexture(target_.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (target_.descriptor_set == VK_NULL_HANDLE) {
      throw std::runtime_error("ImGui_ImplVulkan_AddTexture returned null descriptor set");
    }
    return true;
  } catch (...) {
    DestroyTarget();
    throw;
  }
}

void PreviewRenderer::DestroyTarget() {
  DestroyPipeline();
  if (device_ == VK_NULL_HANDLE && allocator_ == VK_NULL_HANDLE) {
    target_ = {};
    return;
  }

  if (target_.descriptor_set != VK_NULL_HANDLE) {
    ImGui_ImplVulkan_RemoveTexture(target_.descriptor_set);
    target_.descriptor_set = VK_NULL_HANDLE;
  }
  if (target_.framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device_, target_.framebuffer, nullptr);
    target_.framebuffer = VK_NULL_HANDLE;
  }
  if (target_.render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device_, target_.render_pass, nullptr);
    target_.render_pass = VK_NULL_HANDLE;
  }
  if (target_.view != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, target_.view, nullptr);
    target_.view = VK_NULL_HANDLE;
  }
  if (target_.image != VK_NULL_HANDLE) {
    vmaDestroyImage(allocator_, target_.image, target_.allocation);
    target_.image = VK_NULL_HANDLE;
    target_.allocation = VK_NULL_HANDLE;
  }
}

bool PreviewRenderer::EnsureTarget() {
  if (target_.descriptor_set != VK_NULL_HANDLE) {
    return true;
  }
  if (target_.image != VK_NULL_HANDLE || target_.view != VK_NULL_HANDLE || target_.render_pass != VK_NULL_HANDLE) {
    DestroyTarget();
  }
  return CreateTarget();
}

bool PreviewRenderer::RecreateBuffers() {
  if (!request_.valid) {
    return false;
  }

  if (pipeline_.buffers.size() != request_.storage_buffers.size()) {
    for (PreviewBufferResource& buffer : pipeline_.buffers) {
      if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
      }
    }
    pipeline_.buffers.clear();
    pipeline_.buffers.resize(request_.storage_buffers.size());
  }

  for (size_t i = 0; i < request_.storage_buffers.size(); ++i) {
    if (!UploadBuffer(&pipeline_.buffers[i], request_.storage_buffers[i])) {
      return false;
    }
  }

  return true;
}

bool PreviewRenderer::CreatePipeline() {
  if (!request_.valid || request_.vertex_shader_path.empty() || request_.fragment_shader_path.empty()) {
    return false;
  }
  if (request_.storage_buffers.empty()) {
    return false;
  }
  if (!EnsureTarget()) {
    return false;
  }

  const std::string shader_key =
    request_.vertex_shader_path + "|" + request_.fragment_shader_path + "|" +
    std::to_string(request_.storage_buffers.size());
  if (pipeline_.valid && pipeline_.shader_key == shader_key) {
    return true;
  }

  DestroyPipeline();

  try {
    const std::string vertex_path = ResolveShaderBinaryPath(request_.vertex_shader_path);
    const std::string fragment_path = ResolveShaderBinaryPath(request_.fragment_shader_path);
    const std::vector<std::byte> vertex_bytes = ReadBinaryFile(vertex_path);
    const std::vector<std::byte> fragment_bytes = ReadBinaryFile(fragment_path);
    if (vertex_bytes.empty() || fragment_bytes.empty()) {
      return false;
    }

    VkShaderModule vertex_shader_module = VK_NULL_HANDLE;
    VkShaderModule fragment_shader_module = VK_NULL_HANDLE;
    struct ShaderModuleCleanup {
      VkDevice device{VK_NULL_HANDLE};
      VkShaderModule* module{nullptr};
      ~ShaderModuleCleanup() {
        if (device != VK_NULL_HANDLE && module && *module != VK_NULL_HANDLE) {
          vkDestroyShaderModule(device, *module, nullptr);
        }
      }
    } vertex_cleanup{device_, &vertex_shader_module}, fragment_cleanup{device_, &fragment_shader_module};

    VkShaderModuleCreateInfo vertex_module_info{};
    vertex_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    vertex_module_info.codeSize = vertex_bytes.size();
    vertex_module_info.pCode = reinterpret_cast<const uint32_t*>(vertex_bytes.data());

    VkShaderModuleCreateInfo fragment_module_info{};
    fragment_module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    fragment_module_info.codeSize = fragment_bytes.size();
    fragment_module_info.pCode = reinterpret_cast<const uint32_t*>(fragment_bytes.data());

    CheckVkResult(vkCreateShaderModule(device_, &vertex_module_info, nullptr, &vertex_shader_module));
    CheckVkResult(vkCreateShaderModule(device_, &fragment_module_info, nullptr, &fragment_shader_module));

    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(request_.storage_buffers.size());
    for (uint32_t i = 0; i < static_cast<uint32_t>(request_.storage_buffers.size()); ++i) {
      VkDescriptorSetLayoutBinding binding{};
      binding.binding = i;
      binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      binding.descriptorCount = 1;
      binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings.push_back(binding);
    }

    VkDescriptorSetLayoutCreateInfo layout_info{};
    layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout_info.bindingCount = static_cast<uint32_t>(bindings.size());
    layout_info.pBindings = bindings.data();
    CheckVkResult(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &pipeline_.descriptor_set_layout));

    VkPipelineLayoutCreateInfo pipeline_layout_info{};
    pipeline_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeline_layout_info.setLayoutCount = 1;
    pipeline_layout_info.pSetLayouts = &pipeline_.descriptor_set_layout;
    VkPushConstantRange push_constant_range{};
    push_constant_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push_constant_range.offset = 0;
    push_constant_range.size = sizeof(PreviewViewportPushConstants);
    pipeline_layout_info.pushConstantRangeCount = 1;
    pipeline_layout_info.pPushConstantRanges = &push_constant_range;
    CheckVkResult(vkCreatePipelineLayout(device_, &pipeline_layout_info, nullptr, &pipeline_.pipeline_layout));

    VkPipelineShaderStageCreateInfo shader_stages[2]{};
    shader_stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shader_stages[0].module = vertex_shader_module;
    shader_stages[0].pName = "main";
    shader_stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shader_stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shader_stages[1].module = fragment_shader_module;
    shader_stages[1].pName = "main";

    VkPipelineInputAssemblyStateCreateInfo input_assembly{};
    input_assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    input_assembly.primitiveRestartEnable = VK_FALSE;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineColorBlendAttachmentState color_blend_attachment{};
    color_blend_attachment.blendEnable = VK_TRUE;
    color_blend_attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    color_blend_attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.colorBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    color_blend_attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    color_blend_attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    color_blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT |
      VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT |
      VK_COLOR_COMPONENT_A_BIT;

    VkPipelineColorBlendStateCreateInfo color_blend{};
    color_blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    color_blend.attachmentCount = 1;
    color_blend.pAttachments = &color_blend_attachment;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineViewportStateCreateInfo viewport_state{};
    viewport_state.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport_state.viewportCount = 1;
    viewport_state.scissorCount = 1;

    const std::vector<VkDynamicState> dynamic_states = DynamicStates();
    VkPipelineDynamicStateCreateInfo dynamic_state{};
    dynamic_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic_state.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
    dynamic_state.pDynamicStates = dynamic_states.data();

    VkPipelineVertexInputStateCreateInfo vertex_input{};
    vertex_input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkGraphicsPipelineCreateInfo pipeline_info{};
    pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline_info.stageCount = 2;
    pipeline_info.pStages = shader_stages;
    pipeline_info.pVertexInputState = &vertex_input;
    pipeline_info.pInputAssemblyState = &input_assembly;
    pipeline_info.pViewportState = &viewport_state;
    pipeline_info.pRasterizationState = &rasterizer;
    pipeline_info.pMultisampleState = &multisample;
    pipeline_info.pColorBlendState = &color_blend;
    pipeline_info.pDynamicState = &dynamic_state;
    pipeline_info.layout = pipeline_.pipeline_layout;
    pipeline_info.renderPass = target_.render_pass;
    pipeline_info.subpass = 0;

    CheckVkResult(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info, nullptr, &pipeline_.pipeline));
    if (!RecreateBuffers()) {
      DestroyPipeline();
      return false;
    }

    pipeline_.shader_key = shader_key;
    pipeline_.buffer_binding_count = static_cast<uint32_t>(request_.storage_buffers.size());
    pipeline_.valid = true;
    return true;
  } catch (...) {
    DestroyPipeline();
    throw;
  }
}

bool PreviewRenderer::EnsurePipeline() {
  return CreatePipeline();
}

bool PreviewRenderer::UpdateBuffers() {
  if (!pipeline_.valid) {
    return false;
  }
  if (pipeline_.buffers.size() != request_.storage_buffers.size()) {
    return RecreateBuffers();
  }

  for (size_t i = 0; i < request_.storage_buffers.size(); ++i) {
    if (!UploadBuffer(&pipeline_.buffers[i], request_.storage_buffers[i])) {
      return false;
    }
  }
  return true;
}

bool PreviewRenderer::Record(VkCommandBuffer command_buffer) {
  if (!request_.valid) {
    return false;
  }
  if (!EnsureTarget()) {
    return false;
  }
  if (!EnsurePipeline()) {
    return false;
  }
  if (!UpdateBuffers()) {
    return false;
  }

  uint32_t instance_count = request_.instance_count;
  if (instance_count == 0u && !request_.storage_buffers.empty()) {
    const RenderPreviewBuffer& first_buffer = request_.storage_buffers.front();
    if (first_buffer.element_stride > 0u) {
      instance_count = static_cast<uint32_t>(first_buffer.bytes.size() / first_buffer.element_stride);
    }
  }
  if (instance_count == 0u) {
    return false;
  }

  VkDescriptorSetAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  alloc_info.descriptorPool = descriptor_pool_;
  alloc_info.descriptorSetCount = 1;
  alloc_info.pSetLayouts = &pipeline_.descriptor_set_layout;
  if (pipeline_.descriptor_set == VK_NULL_HANDLE) {
    CheckVkResult(vkAllocateDescriptorSets(device_, &alloc_info, &pipeline_.descriptor_set));
  }

  std::vector<VkDescriptorBufferInfo> buffer_infos;
  buffer_infos.reserve(pipeline_.buffers.size());
  for (const PreviewBufferResource& buffer : pipeline_.buffers) {
    VkDescriptorBufferInfo info{};
    info.buffer = buffer.buffer;
    info.offset = 0u;
    info.range = buffer.size_bytes;
    buffer_infos.push_back(info);
  }

  std::vector<VkWriteDescriptorSet> descriptor_writes;
  descriptor_writes.reserve(buffer_infos.size());
  for (uint32_t i = 0; i < static_cast<uint32_t>(buffer_infos.size()); ++i) {
    VkWriteDescriptorSet write{}; 
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = pipeline_.descriptor_set;
    write.dstBinding = i;
    write.dstArrayElement = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_infos[i];
    descriptor_writes.push_back(write);
  }
  if (!descriptor_writes.empty()) {
    vkUpdateDescriptorSets(
      device_,
      static_cast<uint32_t>(descriptor_writes.size()),
      descriptor_writes.data(),
      0,
      nullptr);
  }

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = static_cast<float>(target_.extent.height);
  viewport.width = static_cast<float>(target_.extent.width);
  viewport.height = -static_cast<float>(target_.extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  VkRect2D scissor{};
  scissor.offset = {0, 0};
  scissor.extent = target_.extent;

  VkRenderPassBeginInfo render_pass_begin_info{};
  render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_pass_begin_info.renderPass = target_.render_pass;
  render_pass_begin_info.framebuffer = target_.framebuffer;
  render_pass_begin_info.renderArea.extent = target_.extent;
  VkClearValue clear_value{};
  clear_value.color.float32[0] = 0.0f;
  clear_value.color.float32[1] = 0.0f;
  clear_value.color.float32[2] = 0.0f;
  clear_value.color.float32[3] = 0.0f;
  render_pass_begin_info.clearValueCount = 1;
  render_pass_begin_info.pClearValues = &clear_value;
  vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.pipeline);
  vkCmdBindDescriptorSets(
    command_buffer,
    VK_PIPELINE_BIND_POINT_GRAPHICS,
    pipeline_.pipeline_layout,
    0,
    1,
    &pipeline_.descriptor_set,
    0,
    nullptr);
  const PreviewViewportPushConstants push_constants{
    static_cast<float>(target_.extent.width),
    static_cast<float>(target_.extent.height),
  };
  vkCmdPushConstants(
    command_buffer,
    pipeline_.pipeline_layout,
    VK_SHADER_STAGE_VERTEX_BIT,
    0,
    sizeof(push_constants),
    &push_constants);
  vkCmdSetViewport(command_buffer, 0, 1, &viewport);
  vkCmdSetScissor(command_buffer, 0, 1, &scissor);
  vkCmdDraw(command_buffer, 4, instance_count, 0, 0);
  vkCmdEndRenderPass(command_buffer);
  return true;
}

void PreviewRenderer::DestroyPipeline() {
  if (device_ == VK_NULL_HANDLE) {
    pipeline_ = {};
    return;
  }

  if (pipeline_.descriptor_set != VK_NULL_HANDLE && descriptor_pool_ != VK_NULL_HANDLE) {
    vkFreeDescriptorSets(device_, descriptor_pool_, 1, &pipeline_.descriptor_set);
    pipeline_.descriptor_set = VK_NULL_HANDLE;
  }
  if (pipeline_.pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device_, pipeline_.pipeline, nullptr);
    pipeline_.pipeline = VK_NULL_HANDLE;
  }
  if (pipeline_.pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device_, pipeline_.pipeline_layout, nullptr);
    pipeline_.pipeline_layout = VK_NULL_HANDLE;
  }
  if (pipeline_.descriptor_set_layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device_, pipeline_.descriptor_set_layout, nullptr);
    pipeline_.descriptor_set_layout = VK_NULL_HANDLE;
  }

  for (PreviewBufferResource& buffer : pipeline_.buffers) {
    if (buffer.buffer != VK_NULL_HANDLE) {
      vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    }
  }
  pipeline_.buffers.clear();
  pipeline_.valid = false;
  pipeline_.shader_key.clear();
  pipeline_.buffer_binding_count = 0u;
  pipeline_.instance_count = 0u;
}

void PreviewRenderer::Destroy() {
  DestroyPipeline();
  DestroyTarget();
  request_.Clear();
  instance_ = VK_NULL_HANDLE;
  physical_device_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  descriptor_pool_ = VK_NULL_HANDLE;
  allocator_ = VK_NULL_HANDLE;
}

}  // namespace runtime_systems
