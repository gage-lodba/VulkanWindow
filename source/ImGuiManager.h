#pragma once

#include <vulkan/vulkan.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

class ImGuiManager {
public:
  ImGuiManager(GLFWwindow *window, VkInstance instance,
               VkPhysicalDevice physicalDevice, VkDevice device,
               uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass,
               uint32_t imageCount);
  ~ImGuiManager();

  // Delete copy constructor and assignment operator
  ImGuiManager(const ImGuiManager &) = delete;
  ImGuiManager &operator=(const ImGuiManager &) = delete;

  void newFrame();
  void render(VkCommandBuffer commandBuffer);

private:
  void setupStyle();

  GLFWwindow *window;
  VkDevice device;
  VkDescriptorPool descriptorPool;
};
