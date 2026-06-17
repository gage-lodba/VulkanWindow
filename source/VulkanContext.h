#pragma once

#include <vulkan/vulkan.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Forward declarations for VMA opaque handle types so consumers don't have to
// pull in vk_mem_alloc.h.
struct VmaAllocator_T;
using VmaAllocator = VmaAllocator_T *;

/// Long-lived Vulkan state that doesn't depend on the swap-chain: instance,
/// debug messenger, surface, physical/logical device, queues, VMA allocator,
/// and pipeline cache. Constructed once per process per window; survives
/// swap-chain rebuilds.
///
/// Members are public so apps can build their own pipelines/resources against
/// the device, allocator, queues, and pipeline cache. Reachable read-only via
/// `Application::getContext()`; treat them as read-only — only VulkanRenderer
/// mutates them.
class VulkanContext {
 public:
  /// Construct the context against the given GLFW window. Throws on any
  /// Vulkan-init failure; if the constructor throws, no resources are leaked.
  /// `enableBestPracticesValidation` only takes effect in Debug builds; ignored
  /// when validation layers are unavailable. `appName` namespaces the on-disk
  /// pipeline-cache directory so apps built on this library don't share one
  /// cache file; it's sanitised to a filesystem-safe name (falling back to
  /// "VulkanWindow" if empty).
  explicit VulkanContext(GLFWwindow *window,
                         bool enableBestPracticesValidation = false,
                         std::string_view appName = "VulkanWindow");
  ~VulkanContext();

  VulkanContext(const VulkanContext &) = delete;
  auto operator=(const VulkanContext &) -> VulkanContext & = delete;
  VulkanContext(VulkanContext &&) = delete;
  auto operator=(VulkanContext &&) -> VulkanContext & = delete;

  /// Test whether a physical device meets our requirements: required
  /// extensions present, suitable queue families, swap-chain support
  /// non-empty against `surface`.
  [[nodiscard]] auto isDeviceSuitable(VkPhysicalDevice physDevice) const
      -> bool;

  VkInstance instance{VK_NULL_HANDLE};
  VkDebugUtilsMessengerEXT debugMessenger{VK_NULL_HANDLE};
  VkSurfaceKHR surface{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
  VkPhysicalDeviceProperties physicalDeviceProperties{};
  VkDevice device{VK_NULL_HANDLE};
  VmaAllocator allocator{VK_NULL_HANDLE};
  VkQueue graphicsQueue{VK_NULL_HANDLE};
  VkQueue presentQueue{VK_NULL_HANDLE};
  uint32_t graphicsQueueFamily{0};
  uint32_t presentQueueFamily{0};
  uint32_t instanceApiVersion{0};
  uint32_t deviceApiVersion{0};
  VkPipelineCache pipelineCache{VK_NULL_HANDLE};

  // Capability flags driven by feature probing in createLogicalDevice.

  /// True iff the device supports & we enabled the 1.2 imagelessFramebuffer
  /// feature. The renderer uses this to switch between traditional and
  /// imageless framebuffer paths.
  bool useImagelessFramebuffer{false};

 private:
  void createInstance(bool enableBestPracticesValidation);
  void setupDebugMessenger();
  void createSurface(GLFWwindow *window);
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createAllocator();
  void createPipelineCache();
  void savePipelineCache() const;
  void cleanup();

  [[nodiscard]] auto checkValidationLayerSupport() const -> bool;
  [[nodiscard]] auto checkDeviceExtensionSupport(
      VkPhysicalDevice physDevice) const -> bool;
  [[nodiscard]] auto getRequiredExtensions() const -> std::vector<const char *>;
  [[nodiscard]] static auto rateDeviceSuitability(VkPhysicalDevice physDevice)
      -> int;

  // Filesystem-safe per-app name for the pipeline-cache directory. Set once in
  // the constructor from the `appName` argument.
  std::string cacheName;

  // The unsanitised app name, surfaced to the driver via VkApplicationInfo so
  // vendor tooling / driver profiles can key on it. Set once in the
  // constructor.
  std::string applicationName;

  // Tracks state required during init that the renderer doesn't need to see.
  bool validationLayersActive{false};
  bool validationFeaturesAvailable{false};
  bool getPhysicalDeviceFeatures2Available{false};
  bool needKHRFeatures2Loader{false};
  bool pipelineCacheExternallySyncSupported{false};
  // VK_KHR_portability_enumeration is exposed by the loader (e.g. when a
  // MoltenVK portability ICD is installed). When set, the extension is enabled
  // and VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR is added so
  // portability devices show up in vkEnumeratePhysicalDevices.
  bool portabilityEnumerationEnabled{false};

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
