/*
  MIT License

  Copyright (c) 2024 Jerimiah

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

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

 private:
  GLFWwindow* window;
  VkAllocationCallbacks* g_Allocator = nullptr;
  VkInstance g_Instance = VK_NULL_HANDLE;
  VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
  VkDevice g_Device = VK_NULL_HANDLE;
  uint32_t g_QueueFamily = (uint32_t)-1;
  VkQueue g_Queue = VK_NULL_HANDLE;
  VkDebugReportCallbackEXT g_DebugReport = VK_NULL_HANDLE;
  VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
  VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;

  ImGui_ImplVulkanH_Window g_MainWindowData;
  int g_MinImageCount = 2;
  bool g_SwapChainRebuild = false;

  // Vulkan stuff
  bool IsExtensionAvailable(const ImVector<VkExtensionProperties>& properties,
                            const char* extension);
  VkPhysicalDevice SetupVulkan_SelectPhysicalDevice();
  void SetupVulkan(ImVector<const char*> instance_extensions);
  void SetupVulkanWindow(ImGui_ImplVulkanH_Window* wd, VkSurfaceKHR surface,
                         int width, int height);
  void CleanupVulkan();
  void CleanupVulkanWindow();
  void FrameRender(ImGui_ImplVulkanH_Window* wd, ImDrawData* draw_data);
  void FramePresent(ImGui_ImplVulkanH_Window* wd);
};