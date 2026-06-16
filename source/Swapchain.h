#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <vector>

// Forward declaration for VMA opaque handle type.
struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T *;

class VulkanContext;
enum class PresentMode;
enum class SurfaceFormatPreference;

/// Owns the swap-chain and everything keyed to it: image views, render pass,
/// depth attachment, and framebuffers. Construction picks surface format /
/// present mode / extent and builds dependents in order.
///
/// Resize handling: call `recreate()` with the (possibly unchanged) preferred
/// present mode. The returned struct flags whether the format or image count
/// changed; the caller uses those to decide whether to rebuild downstream
/// state (the ImGui Vulkan backend pins ImageCount at init).
class Swapchain {
 public:
  struct RecreateResult {
    bool formatChanged{false};
    bool imageCountChanged{false};
    /// When `formatChanged` is true, the old render-pass handle is preserved
    /// here so the caller can tear down anything that pinned it (e.g. the
    /// ImGui Vulkan backend) before destroying it. The caller MUST call
    /// vkDestroyRenderPass on this handle, then it's done. VK_NULL_HANDLE
    /// when no rebuild was needed.
    VkRenderPass oldRenderPassToDestroy{VK_NULL_HANDLE};
  };

  Swapchain(VulkanContext &ctx, GLFWwindow *window, PresentMode preferred,
            SurfaceFormatPreference formatPreference);
  ~Swapchain();

  Swapchain(const Swapchain &) = delete;
  auto operator=(const Swapchain &) -> Swapchain & = delete;
  Swapchain(Swapchain &&) = delete;
  auto operator=(Swapchain &&) -> Swapchain & = delete;

  /// Tear down and rebuild against the surface's current state. The window
  /// being 0×0 is the caller's problem — Swapchain assumes a non-zero
  /// framebuffer and will throw on a bad extent. Pre-flight that yourself.
  auto recreate(PresentMode preferred) -> RecreateResult;

  VkSwapchainKHR swapChain{VK_NULL_HANDLE};
  std::vector<VkImage> images;
  std::vector<VkImageView> imageViews;
  std::vector<VkFramebuffer> framebuffers;
  uint32_t minImageCount{0};
  VkFormat imageFormat{};
  VkExtent2D extent{};

  VkRenderPass renderPass{VK_NULL_HANDLE};

  // Depth attachments owned by the swap-chain (resolution must match). One per
  // swap-chain image, indexed by the acquired image index: concurrent
  // in-flight frames always hold distinct image indices, so a per-image depth
  // buffer guarantees no two overlapping frames write the same depth image.
  std::vector<VkImage> depthImages;
  std::vector<VmaAllocation> depthAllocations;
  std::vector<VkImageView> depthImageViews;
  VkFormat depthFormat{VK_FORMAT_UNDEFINED};

 private:
  void createSwapchain(PresentMode preferred);
  void createImageViews();
  void createDepthResources();
  void createRenderPass();
  void createFramebuffers();
  // Destroy the swap-chain-keyed resources (framebuffers, image views, depth
  // images/views) shared by recreate() and cleanup(). Leaves the render pass
  // and swap-chain handle untouched.
  void destroyImageResources();
  void cleanup();

  [[nodiscard]] auto findDepthFormat() const -> VkFormat;
  [[nodiscard]] auto findSupportedFormat(
      const std::vector<VkFormat> &candidates, VkImageTiling tiling,
      VkFormatFeatureFlags features) const -> VkFormat;

  VulkanContext &ctx;
  GLFWwindow *window;
  // Surface-format encoding preference (UNORM vs sRGB). Fixed at construction
  // and reused on every recreate() so the format choice is stable across
  // resizes.
  SurfaceFormatPreference formatPreference;
};
