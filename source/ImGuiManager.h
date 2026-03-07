#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <functional>

class ImGuiManager {
 public:
  ImGuiManager(GLFWwindow *window, VkInstance instance,
               VkPhysicalDevice physicalDevice, VkDevice device,
               uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass,
               VkPipelineCache pipelineCache, uint32_t minImageCount,
               uint32_t imageCount);
  ~ImGuiManager();

  // Delete copy constructor and assignment operator
  ImGuiManager(const ImGuiManager &) = delete;
  auto operator=(const ImGuiManager &) -> ImGuiManager & = delete;

  void newFrame();
  void render(VkCommandBuffer commandBuffer);

  /// Set a callback to customise the ImGui style/theme.
  /// Called once immediately (and can be called again to re-apply).
  /// If no callback is set, the built-in dark theme is used.
  void setStyleCallback(std::function<void()> callback);

 private:
  void setupStyle();

  VkDevice device;
  VkDescriptorPool descriptorPool;
  std::function<void()> styleCallback;
};
