#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

// Forward declarations for VMA opaque handle types
struct VmaAllocator_T;
struct VmaAllocation_T;
using VmaAllocator = VmaAllocator_T *;
using VmaAllocation = VmaAllocation_T *;

class ImGuiManager;

class VulkanRenderer {
 public:
  explicit VulkanRenderer(GLFWwindow *window);
  ~VulkanRenderer();

  VulkanRenderer(const VulkanRenderer &) = delete;
  auto operator=(const VulkanRenderer &) -> VulkanRenderer & = delete;
  VulkanRenderer(VulkanRenderer &&) = delete;
  auto operator=(VulkanRenderer &&) -> VulkanRenderer & = delete;

  void drawFrame();
  void notifyResized() noexcept { framebufferResized = true; }

  /// Set a callback invoked each frame between ImGui::NewFrame() and
  /// ImGui::Render(). Use this to issue your own ImGui draw calls.
  void setUICallback(std::function<void()> callback);

  /// Set a callback to customise the ImGui style/theme.
  /// Replaces the built-in dark theme. Applied immediately.
  void setStyleCallback(std::function<void()> callback);

 private:
  void waitIdle() const;

  void createInstance();
  void setupDebugMessenger();
  void createSurface();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createAllocator();
  void createSwapChain();
  void createImageViews();
  void createRenderPass();
  void createFramebuffers();
  void createCommandPool();
  void createCommandBuffers();
  void createSyncObjects();
  void createDepthResources();
  void createPipelineCache();
  void createImGui();
  void cleanup();
  void savePipelineCache() const;

  void cleanupSwapChain();
  void recreateSwapChain();

  [[nodiscard]] auto checkValidationLayerSupport() const -> bool;
  [[nodiscard]] auto isDeviceSuitable(VkPhysicalDevice device) const -> bool;
  [[nodiscard]] auto checkDeviceExtensionSupport(VkPhysicalDevice device) const
      -> bool;
  [[nodiscard]] auto getRequiredExtensions() const -> std::vector<const char *>;
  [[nodiscard]] auto findDepthFormat() const -> VkFormat;
  [[nodiscard]] auto findSupportedFormat(
      const std::vector<VkFormat> &candidates, VkImageTiling tiling,
      VkFormatFeatureFlags features) const -> VkFormat;
  [[nodiscard]] auto rateDeviceSuitability(VkPhysicalDevice device) const
      -> int;

  GLFWwindow *window;

  VkInstance instance;
  VkDebugUtilsMessengerEXT debugMessenger;
  VkSurfaceKHR surface;
  VkPhysicalDevice physicalDevice;
  VkDevice device;
  VmaAllocator allocator{VK_NULL_HANDLE};
  VkQueue graphicsQueue;
  VkQueue presentQueue;
  uint32_t graphicsQueueFamily{0};
  uint32_t presentQueueFamily{0};
  uint32_t deviceApiVersion{0};

  VkSwapchainKHR swapChain;
  std::vector<VkImage> swapChainImages;
  uint32_t swapChainMinImageCount{0};
  VkFormat swapChainImageFormat{};
  VkExtent2D swapChainExtent{};
  std::vector<VkImageView> swapChainImageViews;
  std::vector<VkFramebuffer> swapChainFramebuffers;

  VkImage depthImage{VK_NULL_HANDLE};
  VmaAllocation depthAllocation{VK_NULL_HANDLE};
  VkImageView depthImageView{VK_NULL_HANDLE};
  VkFormat depthFormat{VK_FORMAT_UNDEFINED};

  VkRenderPass renderPass;
  VkPipelineCache pipelineCache{VK_NULL_HANDLE};
  VkCommandPool commandPool;
  std::vector<VkCommandBuffer> commandBuffers;

  std::vector<VkSemaphore> imageAvailableSemaphores;
  std::vector<VkSemaphore> renderFinishedSemaphores;
  std::vector<VkFence> inFlightFences;
  uint32_t currentFrame{0};
  bool framebufferResized{false};
  bool useImagelessFramebuffer{false};

  std::unique_ptr<ImGuiManager> imguiManager;
  std::function<void()> uiCallback;

  static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

  static constexpr std::array deviceExtensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};

  static constexpr std::array validationLayers = {
      "VK_LAYER_KHRONOS_validation"};

#ifdef NDEBUG
  static constexpr bool enableValidationLayers = false;
#else
  static constexpr bool enableValidationLayers = true;
#endif
};
