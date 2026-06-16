#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <functional>

class ImGuiManager {
 public:
  /// `textureSlots` caps how many distinct textures ImGui can hold
  /// simultaneously (each ImGui_ImplVulkan_AddTexture consumes one slot). The
  /// backend-owned descriptor pool is sized with one extra slot for ImGui's
  /// own font atlas so the slot count maps 1:1 to user textures.
  /// `styleCallback`, if non-empty, replaces the built-in dark theme during
  /// construction so the default isn't applied first and then overwritten.
  /// `linearizeStyleColors` converts the built-in theme's palette from sRGB to
  /// linear (only affects the built-in theme, not a user `styleCallback`); set
  /// it when the swap-chain format is sRGB so colours aren't double-encoded.
  /// `fontCallback`, if non-empty, is invoked during construction to load fonts
  /// into the atlas (and again whenever the ImGui context is rebuilt).
  ImGuiManager(GLFWwindow *window, uint32_t apiVersion, VkInstance instance,
               VkPhysicalDevice physicalDevice, VkDevice device,
               uint32_t queueFamily, VkQueue queue, VkRenderPass renderPass,
               VkPipelineCache pipelineCache, uint32_t minImageCount,
               uint32_t imageCount, uint32_t textureSlots = 100,
               std::function<void()> styleCallback = {},
               bool linearizeStyleColors = false,
               std::function<void()> fontCallback = {});
  ~ImGuiManager();

  // Delete copy constructor and assignment operator
  ImGuiManager(const ImGuiManager &) = delete;
  auto operator=(const ImGuiManager &) -> ImGuiManager & = delete;

  void newFrame();
  void render(VkCommandBuffer commandBuffer);

  /// Update and render ImGui's secondary platform windows (multi-viewport).
  /// Must be called AFTER the main viewport has been submitted to the GPU
  /// and BEFORE its present, on the same thread as drawFrame(). No-op when
  /// the docking branch isn't in use or `ImGuiConfigFlags_ViewportsEnable`
  /// isn't set.
  void renderPlatformWindows();

  /// Inform the Vulkan backend that the swap-chain's min image count
  /// changed (e.g. after a surface reconfig). Must be called outside of an
  /// ImGui frame.
  void setMinImageCount(uint32_t minImageCount);

  /// Set a callback to customise the ImGui style/theme.
  /// Called once immediately (and can be called again to re-apply).
  /// If no callback is set, the built-in dark theme is used.
  void setStyleCallback(std::function<void()> callback);

  /// Set a callback that loads fonts into ImGui's atlas. Called once
  /// immediately (and again whenever the ImGui context is rebuilt). Add fonts
  /// via `ImGui::GetIO().Fonts->AddFontFromFileTTF(...)` inside it; the Vulkan
  /// backend uploads the atlas lazily, so no manual texture build is needed.
  void setFontCallback(std::function<void()> callback);

 private:
  void setupStyle();

  std::function<void()> styleCallback;
  std::function<void()> fontCallback;
  // When true, setupStyle() converts the built-in theme's palette from sRGB to
  // linear so it isn't double-encoded on an sRGB swap-chain.
  bool linearizeStyleColors{false};
};
