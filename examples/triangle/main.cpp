// Minimal example: draw a coloured triangle with your own VkPipeline,
// composited under ImGui. Shows the full "bring your own geometry" path on top
// of the VulkanWindow scaffold:
//
//   * build a pipeline against the handles exposed by Application::getContext()
//     / getSwapchain() (no forking required),
//   * record draws from setRenderCallback,
//   * use dynamic viewport+scissor so a plain resize needs no pipeline work,
//   * rebuild only on a surface-format change via
//   setSwapchainRecreatedCallback,
//   * reuse the scaffold's persistent VkPipelineCache.
//
// The triangle has no vertex buffer — positions/colours are baked into the
// vertex shader and indexed by gl_VertexIndex — to keep the focus on the
// pipeline and callback wiring rather than resource management.

#include <imgui.h>
#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>

#include "Application.h"
#include "Swapchain.h"
#include "VulkanContext.h"
#include "VulkanRenderer.h"  // PresentMode, SwapchainRecreateInfo

namespace {

// SPIR-V compiled by CMake (glslc -mfmt=c) into a C initializer list. A raw
// uint32_t array is exactly the shape VkShaderModuleCreateInfo::pCode expects.
constexpr uint32_t kTriangleVertSpv[] =
#include "triangle.vert.spv.inc"
    ;
constexpr uint32_t kTriangleFragSpv[] =
#include "triangle.frag.spv.inc"
    ;

auto createShaderModule(VkDevice device, const uint32_t *code,
                        size_t sizeBytes) -> VkShaderModule {
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

// Owns the triangle's pipeline and wires it into an Application. Construct it
// after the Application (so the device exists) and destroy it before the
// Application (so the device is still alive) — local-variable ordering in
// main() does exactly that.
class TriangleExample {
 public:
  explicit TriangleExample(Application &app) : app(app) {
    const VulkanContext &ctx = app.getContext();
    device = ctx.device;
    pipelineCache = ctx.pipelineCache;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) !=
        VK_SUCCESS) {
      throw std::runtime_error("Failed to create pipeline layout");
    }

    buildPipeline();

    app.setRenderCallback(
        [this](VkCommandBuffer cmd, VkExtent2D extent) -> void {
          record(cmd, extent);
        });

    // The pipeline bakes in the render pass, so only a surface-format change
    // (which rebuilds the render pass) makes it incompatible. Dynamic
    // viewport+scissor means a plain resize doesn't. The device is idle inside
    // this callback, so destroying the old pipeline here is safe.
    app.setSwapchainRecreatedCallback(
        [this](const SwapchainRecreateInfo &info) -> void {
          if (info.formatChanged) {
            vkDestroyPipeline(device, pipeline, nullptr);
            pipeline = VK_NULL_HANDLE;
            buildPipeline();
          }
        });

    app.setUICallback([]() -> void {
      ImGui::Begin("Triangle example");
      ImGui::TextUnformatted(
          "A VkPipeline drawn via setRenderCallback, under ImGui.");
      ImGui::Text("%.1f FPS", static_cast<double>(ImGui::GetIO().Framerate));
      ImGui::End();
    });
  }

  ~TriangleExample() {
    // run() has returned; wait for the last in-flight frame before teardown.
    vkDeviceWaitIdle(device);
    if (pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, pipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
    }
  }

  TriangleExample(const TriangleExample &) = delete;
  auto operator=(const TriangleExample &) -> TriangleExample & = delete;
  TriangleExample(TriangleExample &&) = delete;
  auto operator=(TriangleExample &&) -> TriangleExample & = delete;

 private:
  void buildPipeline() {
    VkRenderPass renderPass = app.getSwapchain().renderPass;

    VkShaderModule vert =
        createShaderModule(device, kTriangleVertSpv, sizeof(kTriangleVertSpv));
    VkShaderModule frag =
        createShaderModule(device, kTriangleFragSpv, sizeof(kTriangleFragSpv));

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    // No vertex buffers — the shader generates its own vertices.
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType =
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport/scissor are dynamic (set per frame from the swap-chain extent),
    // so the pipeline survives a resize untouched.
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
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // The scaffold's render pass has a depth attachment, so a compatible
    // pipeline must supply depth-stencil state even though we don't test depth.
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

    // Created against the scaffold's persistent cache — warm on the 2nd run.
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

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(extent.width);
    viewport.height = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {.x = 0, .y = 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdDraw(cmd, 3, 1, 0, 0);
  }

  Application &app;
  VkDevice device{VK_NULL_HANDLE};
  VkPipelineCache pipelineCache{VK_NULL_HANDLE};
  VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
  VkPipeline pipeline{VK_NULL_HANDLE};
};

auto main() -> int {
  try {
    Application app(1280, 720, true, "VulkanWindow — Triangle");

    // Constructed after app, destroyed before it (reverse order), so the
    // pipeline is torn down while the device is still alive.
    TriangleExample triangle(app);

    app.run();
  } catch (const std::exception &e) {
    std::cerr << "Fatal error: " << e.what() << '\n';
    return 1;
  }

  return 0;
}
