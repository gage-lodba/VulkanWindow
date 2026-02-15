#include "ImGuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>

ImGuiManager::ImGuiManager(GLFWwindow *window, VkInstance instance,
                           VkPhysicalDevice physicalDevice, VkDevice device,
                           VkAllocationCallbacks *allocator,
                           uint32_t queueFamily, VkQueue queue,
                           VkRenderPass renderPass, uint32_t imageCount)
    : window(window), device(device), allocator(allocator),
      descriptorPool(VK_NULL_HANDLE) {

  // Create descriptor pool for ImGui
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
  };

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 100;
  pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]);
  pool_info.pPoolSizes = pool_sizes;

  if (vkCreateDescriptorPool(device, &pool_info, allocator, &descriptorPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create ImGui descriptor pool");
  }

  // Setup ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;

  // Setup ImGui style
  setupStyle();

  // Initialize ImGui for GLFW
  ImGui_ImplGlfw_InitForVulkan(window, true);

  // Initialize ImGui for Vulkan
  ImGui_ImplVulkan_InitInfo init_info = {};
  init_info.Instance = instance;
  init_info.PhysicalDevice = physicalDevice;
  init_info.Device = device;
  init_info.QueueFamily = queueFamily;
  init_info.Queue = queue;
  init_info.PipelineCache = VK_NULL_HANDLE;
  init_info.DescriptorPool = descriptorPool;
  init_info.RenderPass = renderPass;
  init_info.Subpass = 0;
  init_info.MinImageCount = imageCount;
  init_info.ImageCount = imageCount;
  init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
  init_info.Allocator = allocator;
  init_info.CheckVkResultFn = nullptr;

  ImGui_ImplVulkan_Init(&init_info);
}

ImGuiManager::~ImGuiManager() {
  ImGui_ImplVulkan_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();

  if (descriptorPool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptorPool, allocator);
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

void ImGuiManager::setupStyle() {
  ImGui::StyleColorsDark();

  ImGuiStyle *style = &ImGui::GetStyle();
  style->WindowRounding = 8.f;
  style->FrameRounding = 8.f;
  style->WindowBorderSize = 0.f;

  style->Colors[ImGuiCol_WindowBg] = ImColor(24, 24, 24);
  style->Colors[ImGuiCol_Text] = ImColor(255, 255, 255);
  style->Colors[ImGuiCol_Button] = ImColor(255.f, 255.f, 255.f, 0.125f);
  style->Colors[ImGuiCol_ButtonHovered] = ImColor(255.f, 255.f, 255.f, 0.25f);
  style->Colors[ImGuiCol_ButtonActive] = ImColor(255.f, 255.f, 255.f, 0.5f);
}
