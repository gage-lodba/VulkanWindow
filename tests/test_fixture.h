#pragma once

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

/// Minimal Vulkan context for GPU-resource unit tests: instance, first physical
/// device with a graphics queue, logical device + queue, VMA allocator, and a
/// command pool.  No GLFW window, surface, or swap chain — the vkutil::
/// resource/descriptor helpers need nothing more.
///
/// If setup fails for any reason (no Vulkan loader/ICD, no suitable device)
/// `valid` stays false and each test must `if (!fixture) return;` — the
/// self-skip pattern that keeps the suite green on machines without Vulkan.
class VulkanTestFixture {
 public:
  VulkanTestFixture();
  ~VulkanTestFixture();

  VulkanTestFixture(const VulkanTestFixture&) = delete;
  auto operator=(const VulkanTestFixture&) -> VulkanTestFixture& = delete;

  explicit operator bool() const noexcept { return valid; }

  // Public handles for tests to use.
  VkInstance instance{VK_NULL_HANDLE};
  VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
  VkDevice device{VK_NULL_HANDLE};
  VkQueue graphicsQueue{VK_NULL_HANDLE};
  uint32_t graphicsQueueFamily{0};
  VmaAllocator allocator{VK_NULL_HANDLE};
  VkCommandPool commandPool{VK_NULL_HANDLE};
  bool valid{false};

  /// Whether VK_EXT_headless_surface was enabled on the instance. Tests that
  /// need a surface (findQueueFamilies, querySwapChainSupport) must call
  /// tryCreateHeadlessSurface() and skip on failure.
  bool hasHeadlessSurfaceExt{false};

  /// Create a headless surface. Returns true on success; `surface` is
  /// VK_NULL_HANDLE on failure (caller should skip the test).
  auto tryCreateHeadlessSurface() -> bool;

  VkSurfaceKHR surface{VK_NULL_HANDLE};

 private:
  void createInstance();
  void pickPhysicalDevice();
  void createLogicalDevice();
  void createAllocator();
  void createCommandPool();
  void teardown();

  static inline bool printedReady{false};
  static inline bool printedSkipped{false};
  uint32_t instanceApiVersion{VK_API_VERSION_1_0};
};

// ---- implementation
// -----------------------------------------------------------

inline VulkanTestFixture::VulkanTestFixture() {
  try {
    createInstance();
    pickPhysicalDevice();
    createLogicalDevice();
    createAllocator();
    createCommandPool();
    valid = true;

    if (!printedReady) {
      VkPhysicalDeviceProperties props{};
      vkGetPhysicalDeviceProperties(physicalDevice, &props);
      std::cerr << "VulkanTestFixture: context ready on '" << props.deviceName
                << "'\n";
      printedReady = true;
    }
  } catch (const std::exception& e) {
    if (!printedSkipped) {
      std::cerr << "VulkanTestFixture: setup failed (" << e.what()
                << ") — GPU-resource tests will skip\n";
      printedSkipped = true;
    }
    teardown();
  }
}

inline VulkanTestFixture::~VulkanTestFixture() { teardown(); }

inline void VulkanTestFixture::createInstance() {
  // Cap the requested API version at the loader's, exactly like
  // VulkanContext::createInstance — requesting more than the loader
  // supports makes vkCreateInstance fail with VK_ERROR_INCOMPATIBLE_DRIVER.
  uint32_t loaderVersion = VK_API_VERSION_1_0;
  if (const auto enumerateVersion =
          reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
              vkGetInstanceProcAddr(VK_NULL_HANDLE,
                                    "vkEnumerateInstanceVersion"));
      enumerateVersion != nullptr) {
    enumerateVersion(&loaderVersion);
  }
  instanceApiVersion = std::min(loaderVersion, VK_API_VERSION_1_3);

  // Probe VK_KHR_portability_enumeration and VK_EXT_headless_surface.
  // Portability: so MoltenVK devices are enumerated.
  // Headless surface: lets tests that need a VkSurfaceKHR run headlessly.
  std::vector<const char*> extensions;
  bool portability = false;
  {
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> avail(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, avail.data());
    for (const auto& e : avail) {
      if (std::string_view(e.extensionName) ==
          VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
        portability = true;
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
      } else if (std::string_view(e.extensionName) ==
                 VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME) {
        hasHeadlessSurfaceExt = true;
        extensions.push_back(VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME);
      }
    }
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "VulkanWindow tests";
  appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  appInfo.pEngineName = "VulkanWindow";
  appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  appInfo.apiVersion = instanceApiVersion;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  if (portability) {
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("vkCreateInstance failed (no Vulkan ICD?)");
  }
}

inline void VulkanTestFixture::pickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance, &count, nullptr);
  if (count == 0) {
    throw std::runtime_error("no Vulkan physical devices");
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance, &count, devices.data());

  // First device with any graphics queue family is enough for these tests.
  for (VkPhysicalDevice dev : devices) {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> qfs(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qfCount, qfs.data());
    for (uint32_t i = 0; i < qfCount; ++i) {
      if ((qfs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        physicalDevice = dev;
        graphicsQueueFamily = i;
        return;
      }
    }
  }
  throw std::runtime_error("no physical device with a graphics queue");
}

inline void VulkanTestFixture::createLogicalDevice() {
  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queueInfo{};
  queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueInfo.queueFamilyIndex = graphicsQueueFamily;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;

  // VK_KHR_portability_subset must be enabled when the device exposes it
  // (MoltenVK). Use the string literal to avoid the beta-gated macro.
  std::vector<const char*> extensions;
  {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount,
                                         nullptr);
    std::vector<VkExtensionProperties> avail(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount,
                                         avail.data());
    for (const auto& e : avail) {
      if (std::string_view(e.extensionName) == "VK_KHR_portability_subset") {
        extensions.push_back("VK_KHR_portability_subset");
        break;
      }
    }
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueInfo;
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
      VK_SUCCESS) {
    throw std::runtime_error("vkCreateDevice failed");
  }
  vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
}

inline void VulkanTestFixture::createAllocator() {
  VkPhysicalDeviceProperties props{};
  vkGetPhysicalDeviceProperties(physicalDevice, &props);
  VmaAllocatorCreateInfo allocInfo{};
  allocInfo.instance = instance;
  allocInfo.physicalDevice = physicalDevice;
  allocInfo.device = device;
  // VMA probes entry points against the instance's API version, so use the
  // min of both — same rationale as VulkanContext::createAllocator.
  allocInfo.vulkanApiVersion = std::min(instanceApiVersion, props.apiVersion);
  if (vmaCreateAllocator(&allocInfo, &allocator) != VK_SUCCESS) {
    throw std::runtime_error("vmaCreateAllocator failed");
  }
}

inline void VulkanTestFixture::createCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = graphicsQueueFamily;
  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("vkCreateCommandPool failed");
  }
}

inline void VulkanTestFixture::teardown() {
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
  if (commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, commandPool, nullptr);
    commandPool = VK_NULL_HANDLE;
  }
  if (allocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(allocator);
    allocator = VK_NULL_HANDLE;
  }
  if (device != VK_NULL_HANDLE) {
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

inline auto VulkanTestFixture::tryCreateHeadlessSurface() -> bool {
  if (!hasHeadlessSurfaceExt || !valid) return false;

  // VK_EXT_headless_surface is enabled on the instance — create the surface
  // via the extension's entry point.
  const auto createFn = reinterpret_cast<PFN_vkCreateHeadlessSurfaceEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateHeadlessSurfaceEXT"));
  if (createFn == nullptr) return false;

  VkHeadlessSurfaceCreateInfoEXT surfaceInfo{};
  surfaceInfo.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
  if (createFn(instance, &surfaceInfo, nullptr, &surface) != VK_SUCCESS) {
    surface = VK_NULL_HANDLE;
    return false;
  }
  return true;
}
