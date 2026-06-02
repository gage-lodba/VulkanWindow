// Capstone example: a textured quad that exercises the whole helper surface —
// a vertex buffer (createDeviceLocalBuffer), a mipmapped texture + sampler
// (createTexture2D / createSampler), a descriptor set (createDescriptorSetLayout
// / createDescriptorPool / allocateDescriptorSet / updateImageSamplerDescriptor),
// GLSL compiled+embedded by vulkanwindow_add_shaders, and a pipeline built
// against the handles from Application::getContext() / getSwapchain(). ImGui
// composites on top; the pipeline rebuilds only on a surface-format change.

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include <imgui.h>

#include "Application.h"
#include "Swapchain.h"
#include "VulkanContext.h"
#include "VulkanDescriptors.h"
#include "VulkanRenderer.h"  // SwapchainRecreateInfo
#include "VulkanResources.h"

namespace {

constexpr uint32_t kTextureSize = 256;

// SPIR-V compiled+embedded by vulkanwindow_add_shaders().
constexpr uint32_t kQuadVertSpv[] =
#include "quad.vert.spv.inc"
    ;
constexpr uint32_t kQuadFragSpv[] =
#include "quad.frag.spv.inc"
    ;

struct Vertex {
  float pos[2];
  float uv[2];
};

// A centred quad as two triangles (no index buffer, to stay minimal).
constexpr std::array<Vertex, 6> kQuad = {{
    {{-0.6F, -0.6F}, {0.0F, 0.0F}},
    {{0.6F, -0.6F}, {1.0F, 0.0F}},
    {{0.6F, 0.6F}, {1.0F, 1.0F}},
    {{-0.6F, -0.6F}, {0.0F, 0.0F}},
    {{0.6F, 0.6F}, {1.0F, 1.0F}},
    {{-0.6F, 0.6F}, {0.0F, 1.0F}},
}};

auto makeCheckerboard(uint32_t size) -> std::vector<uint8_t> {
  std::vector<uint8_t> pixels(static_cast<size_t>(size) * size * 4);
  for (uint32_t y = 0; y < size; ++y) {
    for (uint32_t x = 0; x < size; ++x) {
      const bool dark = (((x / 32) + (y / 32)) & 1U) != 0U;
      const size_t i = (static_cast<size_t>(y) * size + x) * 4;
      pixels[i + 0] = dark ? 60 : 240;
      pixels[i + 1] = dark ? 120 : 240;
      pixels[i + 2] = dark ? 220 : 240;
      pixels[i + 3] = 255;
    }
  }
  return pixels;
}

auto createShaderModule(VkDevice device, const uint32_t *code, size_t sizeBytes)
    -> VkShaderModule {
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = sizeBytes;
  info.pCode = code;
  VkShaderModule shaderModule = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &shaderModule) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create shader module");
  }
  return shaderModule;
}

}  // namespace

class TexturedQuadExample {
 public:
  explicit TexturedQuadExample(Application &app) : app(app) {
    const VulkanContext &ctx = app.getContext();
    device = ctx.device;
    allocator = ctx.allocator;
    pipelineCache = ctx.pipelineCache;

    // Vertex buffer (device-local, staged upload).
    vertexBuffer = vkutil::createDeviceLocalBuffer(
        allocator, device, ctx.graphicsQueue, ctx.graphicsQueueFamily,
        kQuad.data(), sizeof(kQuad), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    // Texture (mipmapped) + sampler. UNORM (not _SRGB) so the checkerboard's
    // authored byte values pass through unchanged to the default Unorm
    // swap-chain — the shader samples and writes them verbatim. A real sRGB
    // colour texture feeding a linear-space lit pipeline would instead use an
    // _SRGB format (and typically an Srgb swap-chain) so the GPU linearises on
    // sample and re-encodes on store.
    const std::vector<uint8_t> pixels = makeCheckerboard(kTextureSize);
    texture = vkutil::createTexture2D(
        allocator, ctx.physicalDevice, device, ctx.graphicsQueue,
        ctx.graphicsQueueFamily, pixels.data(), pixels.size(), kTextureSize,
        kTextureSize, VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/true);
    sampler = vkutil::createSampler(device);

    // Descriptor set: one combined image sampler, pointed at the texture.
    descriptorSetLayout = vkutil::createDescriptorSetLayout(
        device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  VK_SHADER_STAGE_FRAGMENT_BIT}});
    descriptorPool = vkutil::createDescriptorPool(
        device, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}}, 1);
    descriptorSet = vkutil::allocateDescriptorSet(device, descriptorPool,
                                                  descriptorSetLayout);
    vkutil::updateImageSamplerDescriptor(device, descriptorSet, 0,
                                         texture.view, sampler);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create pipeline layout");
    }

    buildPipeline();

    app.setRenderCallback([this](VkCommandBuffer cmd, VkExtent2D extent) -> void {
      record(cmd, extent);
    });

    // Only a surface-format change makes the pipeline incompatible (new render
    // pass); dynamic viewport+scissor handles plain resizes. Device is idle in
    // this callback, so rebuilding here is safe.
    app.setSwapchainRecreatedCallback(
        [this](const SwapchainRecreateInfo &info) -> void {
          if (info.formatChanged) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
            buildPipeline();
          }
        });
  }

  ~TexturedQuadExample() {
    vkDeviceWaitIdle(device);
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
    if (descriptorPool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
    }
    if (sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device, sampler, nullptr);
    }
    vkutil::destroyImage(allocator, device, texture);
    vkutil::destroyBuffer(allocator, vertexBuffer);
  }

  TexturedQuadExample(const TexturedQuadExample &) = delete;
  auto operator=(const TexturedQuadExample &) -> TexturedQuadExample & = delete;
  TexturedQuadExample(TexturedQuadExample &&) = delete;
  auto operator=(TexturedQuadExample &&) -> TexturedQuadExample & = delete;

 private:
  void buildPipeline() {
    VkRenderPass renderPass = app.getSwapchain().renderPass;

    VkShaderModule vert =
        createShaderModule(device, kQuadVertSpv, sizeof(kQuadVertSpv));
    VkShaderModule frag =
        createShaderModule(device, kQuadFragSpv, sizeof(kQuadFragSpv));

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrs{};
    attrs[0].location = 0;
    attrs[0].binding = 0;
    attrs[0].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[0].offset = offsetof(Vertex, pos);
    attrs[1].location = 1;
    attrs[1].binding = 0;
    attrs[1].format = VK_FORMAT_R32G32_SFLOAT;
    attrs[1].offset = offsetof(Vertex, uv);

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<uint32_t>(attrs.size());
    vertexInput.pVertexAttributeDescriptions = attrs.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType =
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0F;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType =
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_FALSE;
    depthStencil.depthWriteEnable = VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType =
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT,
                                VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount =
        static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    const VkResult result = vkCreateGraphicsPipelines(
        device, pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);

    vkDestroyShaderModule(device, frag, nullptr);
    vkDestroyShaderModule(device, vert, nullptr);

    if (result != VK_SUCCESS) {
      throw std::runtime_error("Failed to create graphics pipeline");
    }
  }

  void record(VkCommandBuffer cmd, VkExtent2D extent) const {
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                            0, 1, &descriptorSet, 0, nullptr);

    VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer.buffer, &offset);

    VkViewport viewport{};
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.maxDepth = 1.0F;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, static_cast<uint32_t>(kQuad.size()), 1, 0, 0);
  }

  Application &app;
  VkDevice device{VK_NULL_HANDLE};
  VmaAllocator allocator{VK_NULL_HANDLE};
  VkPipelineCache pipelineCache{VK_NULL_HANDLE};

  vkutil::Buffer vertexBuffer;
  vkutil::Image texture;
  VkSampler sampler{VK_NULL_HANDLE};
  VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
  VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
  VkDescriptorSet descriptorSet{VK_NULL_HANDLE};
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
};

auto main() -> int {
  try {
    Application app(1280, 720, true, "VulkanWindow — Textured Quad");
    TexturedQuadExample quad(app);
    app.setUICallback([]() -> void {
      ImGui::Begin("Textured quad");
      ImGui::TextUnformatted("Vertex buffer + mipmapped texture + sampler +");
      ImGui::TextUnformatted("descriptor set, all via vkutil:: helpers.");
      ImGui::Text("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));
      ImGui::End();
    });
    app.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
