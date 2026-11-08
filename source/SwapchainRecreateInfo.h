#pragma once

#include <vulkan/vulkan.h>

/// Details handed to the `setSwapchainRecreatedCallback` callback
/// (Application / VulkanRenderer).
///
/// This lives in its own header rather than alongside the renderer so that a
/// consumer reacting to a swap-chain rebuild can read these fields
/// without including "VulkanRenderer.h" — that header pulls in
/// <GLFW/glfw3.h> plus the renderer's private state, none of which a callback
/// that just wants the new extent should have to depend on.
///
/// Deliberately here and not in RendererTypes.h: that header is kept free of
/// third-party includes so Application.h can name the present-mode and
/// surface-format enums without dragging in GLFW/Vulkan. `extent` below is a
/// VkExtent2D, so this struct cannot live there without breaking that.
struct SwapchainRecreateInfo {
  /// The surface format (and therefore the main render pass) changed. Pipelines
  /// built against the old render pass are now incompatible and must be
  /// recreated against the new `getSwapchain().renderPass`.
  bool formatChanged{false};
  /// The number of swap-chain images changed. Resize any of your own per-image
  /// resources accordingly.
  bool imageCountChanged{false};
  /// The renderer tore down and recreated its ImGui context (and the GLFW +
  /// Vulkan backends behind it) for this rebuild. This happens when the surface
  /// format, the swap-chain image count, or the ImGui minimum image count
  /// changed. Every handle obtained from the ImGui Vulkan backend — notably the
  /// VkDescriptorSet from ImGui_ImplVulkan_AddTexture — is destroyed with the
  /// context and must be re-registered in the callback. Note this can be true
  /// while `formatChanged` is false (image-count-only rebuilds).
  bool imguiContextRebuilt{false};
  /// The new swap-chain extent in pixels.
  VkExtent2D extent{};
};
