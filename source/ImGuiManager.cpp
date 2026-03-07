#include "ImGuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <array>
#include <stdexcept>

ImGuiManager::ImGuiManager(GLFWwindow *window, VkInstance instance,
                           VkPhysicalDevice physicalDevice, VkDevice device,
                           uint32_t queueFamily, VkQueue queue,
                           VkRenderPass renderPass,
                           VkPipelineCache pipelineCache,
                           uint32_t minImageCount, uint32_t imageCount)
    : device(device), descriptorPool(VK_NULL_HANDLE) {
  // Create descriptor pool for ImGui
  std::array<VkDescriptorPoolSize, 1> poolSizes = {
      {{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 100}},
  };

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = 100;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  if (vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create ImGui descriptor pool");
  }

  // Setup ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;  // Disable layout persistence (stateless UI)

  // Setup default ImGui style (can be overridden via setStyleCallback)
  setupStyle();

  // Initialize ImGui for GLFW
  ImGui_ImplGlfw_InitForVulkan(window, true);

  // Initialize ImGui for Vulkan
  ImGui_ImplVulkan_InitInfo initInfo{};
  initInfo.Instance = instance;
  initInfo.PhysicalDevice = physicalDevice;
  initInfo.Device = device;
  initInfo.QueueFamily = queueFamily;
  initInfo.Queue = queue;
  initInfo.PipelineCache = pipelineCache;
  initInfo.DescriptorPool = descriptorPool;
  initInfo.PipelineInfoMain.RenderPass = renderPass;
  initInfo.PipelineInfoMain.Subpass = 0;
  initInfo.MinImageCount = minImageCount;
  initInfo.ImageCount = imageCount;
  initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  initInfo.Allocator = nullptr;
  initInfo.CheckVkResultFn = nullptr;

  ImGui_ImplVulkan_Init(&initInfo);
}

ImGuiManager::~ImGuiManager() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
  }
}

void ImGuiManager::newFrame() {
  ImGui_ImplVulkan_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void ImGuiManager::render(VkCommandBuffer commandBuffer) {
  ImGui::Render();
  ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);
}

void ImGuiManager::setStyleCallback(std::function<void()> callback) {
  styleCallback = std::move(callback);
  // Apply immediately so the new style takes effect right away
  if (styleCallback) {
    styleCallback();
  }
}

void ImGuiManager::setupStyle() {
  if (styleCallback) {
    styleCallback();
    return;
  }

  // Default built-in dark theme
  ImGui::StyleColorsDark();

  ImGuiStyle *style = &ImGui::GetStyle();
  style->WindowRounding = 8.f;
  style->FrameRounding = 8.f;
  style->WindowBorderSize = 0.f;

  style->Colors[ImGuiCol_WindowBg] = ImColor(24, 24, 24, 255);
  style->Colors[ImGuiCol_Text] = ImColor(255, 255, 255, 255);
  style->Colors[ImGuiCol_Button] = ImColor(1.0f, 1.0f, 1.0f, 0.125f);
  style->Colors[ImGuiCol_ButtonHovered] = ImColor(1.0f, 1.0f, 1.0f, 0.25f);
  style->Colors[ImGuiCol_ButtonActive] = ImColor(1.0f, 1.0f, 1.0f, 0.5f);
}
