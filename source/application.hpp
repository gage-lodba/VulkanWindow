#pragma once

#include <stdio.h>
#include <stdlib.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

class Application {
 public:
  Application();
  ~Application();

  void Initialize();
  void Uninitialize();

  void Run();

  bool isRunning();

  GLFWwindow* GetWindowHandle() const { return m_WindowHandle; }

  static VkInstance GetInstance();
  static VkPhysicalDevice GetPhysicalDevice();
  static VkDevice GetDevice();

 private:
  bool Running;

  GLFWwindow* m_WindowHandle;
};