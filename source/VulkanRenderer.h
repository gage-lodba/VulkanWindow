#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class ImGuiManager;
class Swapchain;
class VulkanContext;

/// Preferred swap-chain present mode. The renderer falls back to
/// VK_PRESENT_MODE_FIFO_KHR (vsync, always supported) when the preferred mode
/// isn't available.
enum class PresentMode {
  /// VK_PRESENT_MODE_FIFO_KHR — vsync. Lowest GPU/power usage. Default.
  Vsync,
  /// VK_PRESENT_MODE_MAILBOX_KHR — tear-free, lower latency than FIFO. Burns
  /// GPU rendering frames that are then discarded.
  Mailbox,
  /// VK_PRESENT_MODE_IMMEDIATE_KHR — no vsync. Tearing, lowest latency.
  Immediate,
};

/// Details handed to the swap-chain-recreated callback.
struct SwapchainRecreateInfo {
  /// The surface format (and therefore the main render pass) changed. Pipelines
  /// built against the old render pass are now incompatible and must be
  /// recreated against the new `getSwapchain().renderPass`.
  bool formatChanged{false};
  /// The number of swap-chain images changed. Resize any of your own per-image
  /// resources accordingly.
  bool imageCountChanged{false};
  /// The new swap-chain extent in pixels.
  VkExtent2D extent{};
};

class VulkanRenderer {
 public:
  explicit VulkanRenderer(GLFWwindow *window,
                          PresentMode presentMode = PresentMode::Vsync,
                          uint32_t framesInFlight = 2);
  ~VulkanRenderer();

  VulkanRenderer(const VulkanRenderer &) = delete;
  auto operator=(const VulkanRenderer &) -> VulkanRenderer & = delete;
  VulkanRenderer(VulkanRenderer &&) = delete;
  auto operator=(VulkanRenderer &&) -> VulkanRenderer & = delete;

  void drawFrame();
  void notifyResized() noexcept { framebufferResized = true; }

  /// Change the preferred present mode at runtime. Triggers a swap-chain
  /// rebuild on the next frame. No-op if `mode` matches the current preference.
  /// Note: the actual mode may still fall back to FIFO if the requested mode
  /// isn't supported by the surface.
  void setPresentMode(PresentMode mode);

  [[nodiscard]] auto getPresentMode() const noexcept -> PresentMode {
    return preferredPresentMode;
  }

  /// Set a callback invoked each frame between ImGui::NewFrame() and
  /// ImGui::Render(). Use this to issue your own ImGui draw calls.
  /// Must be called from the thread that drives drawFrame() (i.e. the main
  /// thread); not safe to invoke concurrently with drawFrame().
  void setUICallback(std::function<void()> callback);

  /// Set a callback invoked each frame inside the render pass, before ImGui
  /// records its draw commands. The command buffer is in a recording state
  /// with the main render pass already begun; record draws against it
  /// directly. `extent` is the current swap-chain extent in pixels (use for
  /// viewport/scissor). ImGui composites on top of whatever is rendered here.
  /// Must be called from the thread that drives drawFrame(); not safe to
  /// invoke concurrently with drawFrame().
  void setRenderCallback(
      std::function<void(VkCommandBuffer, VkExtent2D)> callback);

  /// Set a callback to customise the ImGui style/theme.
  /// Replaces the built-in dark theme. Applied immediately.
  /// Must be called from the thread that drives drawFrame(); not safe to
  /// invoke concurrently with drawFrame().
  void setStyleCallback(std::function<void()> callback);

  /// Set a callback invoked right after the swap-chain is rebuilt (resize,
  /// present-mode switch, or surface-format change). The device is idle when it
  /// fires, so it's safe to destroy and recreate your own GPU resources inside
  /// it. Not called for the initial swap-chain — build those resources before
  /// run(). Must be set from / fires on the thread that drives drawFrame().
  void setSwapchainRecreatedCallback(
      std::function<void(const SwapchainRecreateInfo &)> callback);

  /// Set the colour cleared into the swap-chain image at the start of each
  /// frame, before the user's render callback runs. Components are linear in
  /// [0, 1]. Default is opaque black.
  void setClearColor(float r, float g, float b, float a = 1.0f) noexcept;

  /// Read-only access to long-lived, swap-chain-independent Vulkan state
  /// (device, physical device, queues, VMA allocator, pipeline cache). Stable
  /// for the renderer's lifetime.
  [[nodiscard]] auto getContext() const noexcept -> const VulkanContext &;

  /// Read-only access to swap-chain-keyed state (render pass, extent, colour/
  /// depth formats, image views, framebuffers). The handles inside are
  /// replaced when the swap-chain is rebuilt (resize / present-mode / format
  /// change), so re-read each frame; don't cache the render pass or formats
  /// across a resize.
  [[nodiscard]] auto getSwapchain() const noexcept -> const Swapchain &;

  /// Number of frames the CPU may queue ahead of the GPU (the constructor
  /// parameter). Size per-frame resources by this; index them with
  /// getCurrentFrameIndex().
  [[nodiscard]] auto getFramesInFlight() const noexcept -> uint32_t {
    return framesInFlight;
  }

  /// Index of the in-flight frame slot currently being recorded, in
  /// [0, getFramesInFlight()). Meaningful while a render callback is running.
  [[nodiscard]] auto getCurrentFrameIndex() const noexcept -> uint32_t {
    return currentFrame;
  }

 private:
  void waitIdle() const;

  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();
  void createPerImageSemaphores();
  // Destroy the per-image render-finished semaphores and clear the vector.
  // Safe to call with a partially-populated or empty vector.
  void destroyRenderFinishedSemaphores();
  // Destroy and recreate the per-frame image-available semaphores in place.
  // Used after vkAcquireNextImageKHR leaves a semaphore indeterminate
  // (VK_ERROR_OUT_OF_DATE_KHR); the caller must have waited for device idle.
  void recreateImageAvailableSemaphores();
  void createImGui();
  void cleanup();

  void recreateSwapChain();

  GLFWwindow *window;
  PresentMode preferredPresentMode{PresentMode::Vsync};

  std::unique_ptr<VulkanContext> context;
  std::unique_ptr<Swapchain> swapchain;

  VkCommandPool commandPool{VK_NULL_HANDLE};
  std::vector<VkCommandBuffer> commandBuffers;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  uint32_t currentFrame{0};
  bool framebufferResized{false};
  std::array<float, 4> clearColor{0.0f, 0.0f, 0.0f, 1.0f};
  // Set when vkAcquireNextImageKHR returns VK_ERROR_OUT_OF_DATE_KHR — the
  // supplied semaphore is left in an indeterminate state per spec, so the
  // imageAvailableSemaphores must be torn down and recreated. Cleared after
  // recreateSwapChain rebuilds them.
  bool acquireSemaphoresInvalid{false};

  std::unique_ptr<ImGuiManager> imguiManager;
  std::function<void()> uiCallback;
  std::function<void(VkCommandBuffer, VkExtent2D)> renderCallback;
  std::function<void()> styleCallback;
  std::function<void(const SwapchainRecreateInfo &)> swapchainRecreatedCallback;

  // Number of frames the CPU may queue ahead of the GPU. Sized at construction;
  // controls the depth of the per-frame fence/semaphore/command-buffer arrays.
  uint32_t framesInFlight{2};

#ifdef VULKAN_WINDOW_BEST_PRACTICES
  static constexpr bool enableBestPracticesValidation = true;
#else
  static constexpr bool enableBestPracticesValidation = false;
#endif
};
