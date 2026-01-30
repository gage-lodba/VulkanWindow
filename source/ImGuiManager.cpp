#include "ImGuiManager.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <stdexcept>

ImGuiManager::ImGuiManager(GLFWwindow *window, VkInstance instance,
                           VkPhysicalDevice physicalDevice, VkDevice device,
                           uint32_t queueFamily, VkQueue queue,
                           VkRenderPass renderPass, uint32_t imageCount)
    : window(window), device(device), descriptorPool(VK_NULL_HANDLE) {

  // Create descriptor pool for ImGui
  VkDescriptorPoolSize pool_sizes[] = {
      {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
      {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
      {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

  VkDescriptorPoolCreateInfo pool_info = {};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  pool_info.maxSets = 1000;
  pool_info.poolSizeCount = sizeof(pool_sizes) / sizeof(pool_sizes[0]);
  pool_info.pPoolSizes = pool_sizes;

  if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create ImGui descriptor pool");
  }

  // Setup ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
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
  init_info.Allocator = nullptr;
  init_info.CheckVkResultFn = nullptr;

  ImGui_ImplVulkan_Init(&init_info);

  VkCommandPool commandPool;
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = queueFamily;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool);

  VkCommandBuffer commandBuffer;
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = commandPool;
  allocInfo.commandBufferCount = 1;
  vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);

  ImGui_ImplVulkan_CreateFontsTexture();

  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(queue);

  vkDestroyCommandPool(device, commandPool, nullptr);
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