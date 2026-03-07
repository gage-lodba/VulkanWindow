#include "VulkanRenderer.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>

#include "ImGuiManager.h"

namespace {

struct QueueFamilyIndices {
  std::optional<uint32_t> graphicsFamily;
  std::optional<uint32_t> presentFamily;

  [[nodiscard]] auto isComplete() const noexcept -> bool {
    return graphicsFamily.has_value() && presentFamily.has_value();
  }
};

struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};

auto findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
    -> QueueFamilyIndices {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  // First pass: prefer a single family that supports both graphics and present
  for (size_t i = 0; i < queueFamilies.size(); i++) {
    if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, static_cast<uint32_t>(i),
                                           surface, &presentSupport);
      if (presentSupport) {
        indices.graphicsFamily = static_cast<uint32_t>(i);
        indices.presentFamily = static_cast<uint32_t>(i);
        return indices;
      }
    }
  }

  // Fallback: find separate families for graphics and present
  for (size_t i = 0; i < queueFamilies.size(); i++) {
    if (!indices.graphicsFamily.has_value() &&
        (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
      indices.graphicsFamily = static_cast<uint32_t>(i);
    }

    if (!indices.presentFamily.has_value()) {
      VkBool32 presentSupport = false;
      vkGetPhysicalDeviceSurfaceSupportKHR(device, static_cast<uint32_t>(i),
                                           surface, &presentSupport);
      if (presentSupport) {
        indices.presentFamily = static_cast<uint32_t>(i);
      }
    }

    if (indices.isComplete()) break;
  }

  return indices;
}

auto querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    -> SwapChainSupportDetails {
  SwapChainSupportDetails details;

  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface,
                                            &details.capabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);

  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount,
                                         details.formats.data());
  }

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount,
                                            nullptr);

  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &presentModeCount, details.presentModes.data());
  }

  return details;
}

auto chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &availableFormats)
    -> VkSurfaceFormatKHR {
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }
  return availableFormats[0];
}

auto chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &availablePresentModes)
    -> VkPresentModeKHR {
  for (const auto &availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      return availablePresentMode;
    }
  }
  return VK_PRESENT_MODE_FIFO_KHR;
}

auto chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities,
                      GLFWwindow *window) -> VkExtent2D {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  }

  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  VkExtent2D actualExtent = {static_cast<uint32_t>(width),
                             static_cast<uint32_t>(height)};

  actualExtent.width =
      std::clamp(actualExtent.width, capabilities.minImageExtent.width,
                 capabilities.maxImageExtent.width);
  actualExtent.height =
      std::clamp(actualExtent.height, capabilities.minImageExtent.height,
                 capabilities.maxImageExtent.height);

  return actualExtent;
}

auto createDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    VkDebugUtilsMessengerEXT *pDebugMessenger) -> VkResult {
  auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    return func(instance, pCreateInfo, nullptr, pDebugMessenger);
  } else {
    return VK_ERROR_EXTENSION_NOT_PRESENT;
  }
}

void destroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger) {
  auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    func(instance, debugMessenger, nullptr);
  }
}

VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void * /*pUserData*/) {
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::cerr << "Validation layer: " << pCallbackData->pMessage << std::endl;
  }
  return VK_FALSE;
}

}  // namespace

VulkanRenderer::VulkanRenderer(GLFWwindow *window)
    : window(window),
      instance(VK_NULL_HANDLE),
      debugMessenger(VK_NULL_HANDLE),
      surface(VK_NULL_HANDLE),
      physicalDevice(VK_NULL_HANDLE),
      device(VK_NULL_HANDLE),
      graphicsQueue(VK_NULL_HANDLE),
      presentQueue(VK_NULL_HANDLE),
      swapChain(VK_NULL_HANDLE),
      renderPass(VK_NULL_HANDLE),
      commandPool(VK_NULL_HANDLE) {
  try {
    createInstance();
    setupDebugMessenger();
    createSurface();
    pickPhysicalDevice();
    createLogicalDevice();
    createAllocator();
    createPipelineCache();
    createSwapChain();
    createImageViews();
    createDepthResources();
    createRenderPass();
    createFramebuffers();
    createCommandPool();
    createCommandBuffers();
    createSyncObjects();
    createImGui();
  } catch (...) {
    cleanup();
    throw;
  }
}

VulkanRenderer::~VulkanRenderer() { cleanup(); }

auto VulkanRenderer::checkValidationLayerSupport() const -> bool {
  uint32_t layerCount = 0;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

  for (const char *layerName : validationLayers) {
    bool layerFound = false;

    for (const auto &layerProperties : availableLayers) {
      if (std::string_view(layerName) == layerProperties.layerName) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

void VulkanRenderer::createInstance() {
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("Validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Vulkan Window";
  appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  // Setup debug messenger info for instance creation/destruction
  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  VkValidationFeaturesEXT validationFeatures{};
  std::array enabledValidationFeatures = {
      VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT,
      VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT};

  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    // Enable debug messenger during instance creation/destruction
    debugCreateInfo.sType =
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    debugCreateInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugCreateInfo.messageType =
        VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugCreateInfo.pfnUserCallback = debugCallback;

    // Enable best practices and synchronization validation
    validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
    validationFeatures.enabledValidationFeatureCount =
        static_cast<uint32_t>(enabledValidationFeatures.size());
    validationFeatures.pEnabledValidationFeatures =
        enabledValidationFeatures.data();
    validationFeatures.pNext = &debugCreateInfo;

    createInfo.pNext = &validationFeatures;
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
  }

  if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create Vulkan instance");
  }
}

void VulkanRenderer::setupDebugMessenger() {
  if (!enableValidationLayers) return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  createInfo.pUserData = nullptr;

  if (createDebugUtilsMessengerEXT(instance, &createInfo, &debugMessenger) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to set up debug messenger!");
  }
}

void VulkanRenderer::createSurface() {
  if (glfwCreateWindowSurface(instance, window, nullptr, &surface) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create window surface");
  }
}

void VulkanRenderer::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

  // Score all suitable devices and pick the best one
  int bestScore = -1;
  for (const auto &dev : devices) {
    if (!isDeviceSuitable(dev)) continue;

    int score = rateDeviceSuitability(dev);
    if (score > bestScore) {
      bestScore = score;
      physicalDevice = dev;
    }
  }

  if (physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to find a suitable GPU");
  }

  // Log the selected device
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physicalDevice, &props);
  std::cout << "Selected GPU: " << props.deviceName << " (Vulkan "
            << VK_API_VERSION_MAJOR(props.apiVersion) << "."
            << VK_API_VERSION_MINOR(props.apiVersion) << "."
            << VK_API_VERSION_PATCH(props.apiVersion) << ")\n";
}

void VulkanRenderer::createLogicalDevice() {
  QueueFamilyIndices indices = findQueueFamilies(physicalDevice, surface);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily.value(),
                                            indices.presentFamily.value()};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  // Query device API version to conditionally enable 1.2/1.3 features
  VkPhysicalDeviceProperties deviceProperties;
  vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
  deviceApiVersion = deviceProperties.apiVersion;

  // Build feature chain: features2 -> features12 [-> features13]
  VkPhysicalDeviceVulkan12Features features12{};
  features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

  VkPhysicalDeviceVulkan13Features features13{};
  bool supports13 = deviceProperties.apiVersion >= VK_API_VERSION_1_3;
  if (supports13) {
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features12.pNext = &features13;
  }

  VkPhysicalDeviceFeatures2 features2{};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  features2.pNext = &features12;

  // Query what the device actually supports
  VkPhysicalDeviceVulkan12Features supported12{};
  supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

  VkPhysicalDeviceVulkan13Features supported13{};
  if (supports13) {
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    supported12.pNext = &supported13;
  }

  VkPhysicalDeviceFeatures2 supportedFeatures2{};
  supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  supportedFeatures2.pNext = &supported12;
  vkGetPhysicalDeviceFeatures2(physicalDevice, &supportedFeatures2);

  // Only request features that are actually supported
  features12.timelineSemaphore = supported12.timelineSemaphore;
  features12.imagelessFramebuffer = supported12.imagelessFramebuffer;
  if (supports13) {
    features13.dynamicRendering = supported13.dynamicRendering;
    features13.synchronization2 = supported13.synchronization2;
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  createInfo.pNext = &features2;          // use features2 via pNext
  createInfo.pEnabledFeatures = nullptr;  // must be null when using pNext
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  }

  if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create logical device");
  }

  graphicsQueueFamily = indices.graphicsFamily.value();
  presentQueueFamily = indices.presentFamily.value();
  vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
  vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);

  useImagelessFramebuffer = (features12.imagelessFramebuffer == VK_TRUE);
}

void VulkanRenderer::createAllocator() {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.instance = instance;
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device = device;
  allocatorInfo.vulkanApiVersion = deviceApiVersion;

  if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create VMA allocator");
  }
}

void VulkanRenderer::createSwapChain() {
  SwapChainSupportDetails swapChainSupport =
      querySwapChainSupport(physicalDevice, surface);

  VkSurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode =
      chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities, window);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = surface;
  createInfo.minImageCount = imageCount;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  std::array queueFamilyIndices = {graphicsQueueFamily, presentQueueFamily};

  if (graphicsQueueFamily != presentQueueFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices.data();
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  swapChainMinImageCount = imageCount;

  VkSwapchainKHR oldSwapchain = swapChain;
  createInfo.oldSwapchain = oldSwapchain;

  if (vkCreateSwapchainKHR(device, &createInfo, nullptr, &swapChain) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create swap chain");
  }

  // Destroy the old swap chain after creating the new one
  if (oldSwapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, oldSwapchain, nullptr);
  }

  vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr);
  swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(device, swapChain, &imageCount,
                          swapChainImages.data());

  swapChainImageFormat = surfaceFormat.format;
  swapChainExtent = extent;
}

void VulkanRenderer::createImageViews() {
  swapChainImageViews.resize(swapChainImages.size());

  for (size_t i = 0; i < swapChainImages.size(); i++) {
    VkImageViewCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    createInfo.image = swapChainImages[i];
    createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    createInfo.format = swapChainImageFormat;
    createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = 1;
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &createInfo, nullptr,
                          &swapChainImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create image views");
    }
  }
}

void VulkanRenderer::createRenderPass() {
  VkAttachmentDescription colorAttachment{};
  colorAttachment.format = swapChainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = depthFormat;
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorAttachmentRef{};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                            VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

  std::array attachments = {colorAttachment, depthAttachment};

  VkRenderPassCreateInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create render pass");
  }
}

void VulkanRenderer::createFramebuffers() {
  if (useImagelessFramebuffer) {
    // Imageless framebuffer (Vulkan 1.2): one framebuffer for all swapchain
    // images; actual image views are provided at vkCmdBeginRenderPass time.
    VkFramebufferAttachmentImageInfo colorAttachmentImageInfo{};
    colorAttachmentImageInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
    colorAttachmentImageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    colorAttachmentImageInfo.width = swapChainExtent.width;
    colorAttachmentImageInfo.height = swapChainExtent.height;
    colorAttachmentImageInfo.layerCount = 1;
    colorAttachmentImageInfo.viewFormatCount = 1;
    colorAttachmentImageInfo.pViewFormats = &swapChainImageFormat;

    VkFramebufferAttachmentImageInfo depthAttachmentImageInfo{};
    depthAttachmentImageInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
    depthAttachmentImageInfo.usage =
        VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthAttachmentImageInfo.width = swapChainExtent.width;
    depthAttachmentImageInfo.height = swapChainExtent.height;
    depthAttachmentImageInfo.layerCount = 1;
    depthAttachmentImageInfo.viewFormatCount = 1;
    depthAttachmentImageInfo.pViewFormats = &depthFormat;

    std::array attachmentImageInfos = {colorAttachmentImageInfo,
                                       depthAttachmentImageInfo};

    VkFramebufferAttachmentsCreateInfo attachmentsCreateInfo{};
    attachmentsCreateInfo.sType =
        VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
    attachmentsCreateInfo.attachmentImageInfoCount =
        static_cast<uint32_t>(attachmentImageInfos.size());
    attachmentsCreateInfo.pAttachmentImageInfos = attachmentImageInfos.data();

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
    framebufferInfo.pNext = &attachmentsCreateInfo;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount =
        static_cast<uint32_t>(attachmentImageInfos.size());
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    swapChainFramebuffers.resize(1);
    if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                            &swapChainFramebuffers[0]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create imageless framebuffer");
    }
  } else {
    // Traditional: one framebuffer per swapchain image.
    swapChainFramebuffers.resize(swapChainImageViews.size());

    for (size_t i = 0; i < swapChainImageViews.size(); i++) {
      std::array attachments = {swapChainImageViews[i], depthImageView};

      VkFramebufferCreateInfo framebufferInfo{};
      framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
      framebufferInfo.renderPass = renderPass;
      framebufferInfo.attachmentCount =
          static_cast<uint32_t>(attachments.size());
      framebufferInfo.pAttachments = attachments.data();
      framebufferInfo.width = swapChainExtent.width;
      framebufferInfo.height = swapChainExtent.height;
      framebufferInfo.layers = 1;

      if (vkCreateFramebuffer(device, &framebufferInfo, nullptr,
                              &swapChainFramebuffers[i]) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create framebuffer");
      }
    }
  }
}

void VulkanRenderer::createCommandPool() {
  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = graphicsQueueFamily;

  if (vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create command pool");
  }
}

void VulkanRenderer::createCommandBuffers() {
  commandBuffers.resize(MAX_FRAMES_IN_FLIGHT);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

  if (vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to allocate command buffers");
  }
}

void VulkanRenderer::createSyncObjects() {
  imageAvailableSemaphores.assign(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);
  inFlightFences.assign(MAX_FRAMES_IN_FLIGHT, VK_NULL_HANDLE);

  // One render-finished semaphore per swapchain image, since the present
  // engine holds the semaphore until the image is re-acquired.
  renderFinishedSemaphores.assign(swapChainImages.size(), VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                          &imageAvailableSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[i]) !=
            VK_SUCCESS) {
      throw std::runtime_error("Failed to create synchronization objects");
    }
  }

  for (size_t i = 0; i < swapChainImages.size(); i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                          &renderFinishedSemaphores[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create render finished semaphore");
    }
  }
}

void VulkanRenderer::createImGui() {
  imguiManager = std::make_unique<ImGuiManager>(
      window, instance, physicalDevice, device, graphicsQueueFamily,
      graphicsQueue, renderPass, pipelineCache, swapChainMinImageCount,
      static_cast<uint32_t>(swapChainImages.size()));
}

void VulkanRenderer::setUICallback(std::function<void()> callback) {
  uiCallback = std::move(callback);
}

void VulkanRenderer::setStyleCallback(std::function<void()> callback) {
  imguiManager->setStyleCallback(std::move(callback));
}

void VulkanRenderer::drawFrame() {
  // Skip rendering when the window is minimized (0×0 framebuffer)
  int fbWidth = 0, fbHeight = 0;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (fbWidth == 0 || fbHeight == 0) {
    return;
  }

  vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE,
                  UINT64_MAX);

  uint32_t imageIndex = 0;
  VkResult result = vkAcquireNextImageKHR(
      device, swapChain, UINT64_MAX, imageAvailableSemaphores[currentFrame],
      VK_NULL_HANDLE, &imageIndex);

  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain();
    return;
  } else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("Failed to acquire swap chain image");
  }

  vkResetCommandBuffer(commandBuffers[currentFrame], 0);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffers[currentFrame], &beginInfo) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to begin recording command buffer");
  }

  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = renderPass;
  renderPassInfo.renderArea.offset = {.x = 0, .y = 0};
  renderPassInfo.renderArea.extent = swapChainExtent;

  VkClearValue clearColor{};
  clearColor.color = {{1.0f, 1.0f, 1.0f, 1.0f}};
  VkClearValue clearDepth{};
  clearDepth.depthStencil = {.depth = 1.0f, .stencil = 0};
  std::array clearValues = {clearColor, clearDepth};
  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  // For imageless framebuffers, provide image views at begin time.
  std::array imagelessAttachments = {swapChainImageViews[imageIndex],
                                     depthImageView};
  VkRenderPassAttachmentBeginInfo attachmentBeginInfo{};
  if (useImagelessFramebuffer) {
    attachmentBeginInfo.sType =
        VK_STRUCTURE_TYPE_RENDER_PASS_ATTACHMENT_BEGIN_INFO;
    attachmentBeginInfo.attachmentCount =
        static_cast<uint32_t>(imagelessAttachments.size());
    attachmentBeginInfo.pAttachments = imagelessAttachments.data();
    renderPassInfo.pNext = &attachmentBeginInfo;
    renderPassInfo.framebuffer = swapChainFramebuffers[0];
  } else {
    renderPassInfo.framebuffer = swapChainFramebuffers[imageIndex];
  }

  vkCmdBeginRenderPass(commandBuffers[currentFrame], &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  // Start ImGui frame
  imguiManager->newFrame();

  // Invoke the user-provided UI callback (if any)
  if (uiCallback) {
    uiCallback();
  }

  // Record ImGui draw commands
  imguiManager->render(commandBuffers[currentFrame]);

  vkCmdEndRenderPass(commandBuffers[currentFrame]);

  if (vkEndCommandBuffer(commandBuffers[currentFrame]) != VK_SUCCESS) {
    throw std::runtime_error("Failed to record command buffer");
  }

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  std::array waitSemaphores = {imageAvailableSemaphores[currentFrame]};
  std::array<VkPipelineStageFlags, 1> waitStages = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores.data();
  submitInfo.pWaitDstStageMask = waitStages.data();
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffers[currentFrame];

  std::array signalSemaphores = {renderFinishedSemaphores[imageIndex]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores.data();

  // Reset the fence only right before submit so it stays signaled if submit
  // fails, preventing vkWaitForFences from deadlocking on the next frame.
  vkResetFences(device, 1, &inFlightFences[currentFrame]);

  VkResult submitResult = vkQueueSubmit(graphicsQueue, 1, &submitInfo,
                                        inFlightFences[currentFrame]);
  if (submitResult != VK_SUCCESS) {
    // Re-signal the fence so the next frame doesn't deadlock on wait.
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkDestroyFence(device, inFlightFences[currentFrame], nullptr);
    vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences[currentFrame]);
    throw std::runtime_error("Failed to submit draw command buffer");
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores.data();

  std::array swapChains = {swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains.data();
  presentInfo.pImageIndices = &imageIndex;

  result = vkQueuePresentKHR(presentQueue, &presentInfo);

  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      framebufferResized) {
    framebufferResized = false;
    recreateSwapChain();
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to present swap chain image");
  }

  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VulkanRenderer::waitIdle() const {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
}

auto VulkanRenderer::isDeviceSuitable(VkPhysicalDevice device) const -> bool {
  QueueFamilyIndices indices = findQueueFamilies(device, surface);
  bool extensionsSupported = checkDeviceExtensionSupport(device);
  bool swapChainAdequate = false;

  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(device, surface);
    swapChainAdequate = !swapChainSupport.formats.empty() &&
                        !swapChainSupport.presentModes.empty();
  }

  return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

auto VulkanRenderer::checkDeviceExtensionSupport(VkPhysicalDevice device) const
    -> bool {
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                           deviceExtensions.end());

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

auto VulkanRenderer::getRequiredExtensions() const
    -> std::vector<const char *> {
  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

  if (enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}

auto VulkanRenderer::findSupportedFormat(
    const std::vector<VkFormat> &candidates, VkImageTiling tiling,
    VkFormatFeatureFlags features) const -> VkFormat {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    }
    if (tiling == VK_IMAGE_TILING_OPTIMAL &&
        (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }

  throw std::runtime_error("Failed to find supported format!");
}

auto VulkanRenderer::findDepthFormat() const -> VkFormat {
  return findSupportedFormat(
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void VulkanRenderer::createDepthResources() {
  depthFormat = findDepthFormat();

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent.width = swapChainExtent.width;
  imageInfo.extent.height = swapChainExtent.height;
  imageInfo.extent.depth = 1;
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = depthFormat;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VmaAllocationCreateInfo allocCreateInfo{};
  allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;

  if (vmaCreateImage(allocator, &imageInfo, &allocCreateInfo, &depthImage,
                     &depthAllocation, nullptr) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create depth image");
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = depthImage;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = depthFormat;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(device, &viewInfo, nullptr, &depthImageView) !=
      VK_SUCCESS) {
    throw std::runtime_error("Failed to create depth image view");
  }
}

void VulkanRenderer::createPipelineCache() {
  VkPipelineCacheCreateInfo cacheInfo{};
  cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

  // Try to load existing cache from disk
  std::vector<char> cacheData;
  std::ifstream cacheFile("pipeline_cache.bin",
                          std::ios::binary | std::ios::ate);
  if (cacheFile.is_open()) {
    auto size = cacheFile.tellg();
    if (size > 0) {
      cacheData.resize(static_cast<size_t>(size));
      cacheFile.seekg(0);
      cacheFile.read(cacheData.data(), size);
    }
    cacheFile.close();

    // Validate cache header against current device (per Vulkan spec, the
    // header is: uint32 headerLength, uint32 headerVersion, uint32 vendorID,
    // uint32 deviceID, uint8 pipelineCacheUUID[VK_UUID_SIZE]).
    constexpr size_t kHeaderSize = 4 + 4 + 4 + 4 + VK_UUID_SIZE;
    if (cacheData.size() >= kHeaderSize) {
      uint32_t headerVersion = 0;
      uint32_t vendorID = 0;
      uint32_t deviceID = 0;
      std::memcpy(&headerVersion, cacheData.data() + 4, sizeof(uint32_t));
      std::memcpy(&vendorID, cacheData.data() + 8, sizeof(uint32_t));
      std::memcpy(&deviceID, cacheData.data() + 12, sizeof(uint32_t));

      VkPhysicalDeviceProperties props;
      vkGetPhysicalDeviceProperties(physicalDevice, &props);

      if (headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
          vendorID == props.vendorID && deviceID == props.deviceID &&
          std::memcmp(cacheData.data() + 16, props.pipelineCacheUUID,
                      VK_UUID_SIZE) == 0) {
        cacheInfo.initialDataSize = cacheData.size();
        cacheInfo.pInitialData = cacheData.data();
      }
    }
  }

  if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) !=
      VK_SUCCESS) {
    // Non-fatal: create an empty cache if loading failed
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;
    vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache);
  }
}

void VulkanRenderer::savePipelineCache() const {
  if (pipelineCache == VK_NULL_HANDLE) return;

  size_t cacheSize = 0;
  vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr);

  if (cacheSize == 0) return;

  std::vector<char> cacheData(cacheSize);
  vkGetPipelineCacheData(device, pipelineCache, &cacheSize, cacheData.data());

  std::ofstream cacheFile("pipeline_cache.bin", std::ios::binary);
  if (cacheFile.is_open()) {
    cacheFile.write(cacheData.data(), static_cast<std::streamsize>(cacheSize));
  }
}

auto VulkanRenderer::rateDeviceSuitability(VkPhysicalDevice device) const
    -> int {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(device, &props);

  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(device, &memProps);

  int score = 0;

  // Strongly prefer discrete GPUs
  if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    score += 10000;
  } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
    score += 1000;
  }

  // Score by device-local VRAM size (in MB)
  for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
    if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
      score += static_cast<int>(memProps.memoryHeaps[i].size / (1024 * 1024));
    }
  }

  // Prefer higher Vulkan API version support
  score += static_cast<int>(VK_API_VERSION_MINOR(props.apiVersion)) * 100;

  return score;
}

void VulkanRenderer::cleanupSwapChain() {
  if (depthImageView != VK_NULL_HANDLE) {
    vkDestroyImageView(device, depthImageView, nullptr);
    depthImageView = VK_NULL_HANDLE;
  }
  if (depthImage != VK_NULL_HANDLE) {
    vmaDestroyImage(allocator, depthImage, depthAllocation);
    depthImage = VK_NULL_HANDLE;
    depthAllocation = VK_NULL_HANDLE;
  }

  for (auto framebuffer : swapChainFramebuffers) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
  }
  swapChainFramebuffers.clear();

  for (auto imageView : swapChainImageViews) {
    vkDestroyImageView(device, imageView, nullptr);
  }
  swapChainImageViews.clear();
}

void VulkanRenderer::recreateSwapChain() {
  int width = 0, height = 0;
  glfwGetFramebufferSize(window, &width, &height);

  // Defer recreation while the window is minimized (0×0 framebuffer).
  // The resize callback will trigger another attempt when restored.
  if (width == 0 || height == 0) return;

  vkDeviceWaitIdle(device);

  cleanupSwapChain();

  // Destroy old per-image render-finished semaphores
  for (auto &semaphore : renderFinishedSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, semaphore, nullptr);
    }
  }

  VkFormat oldFormat = swapChainImageFormat;
  createSwapChain();

  // Recreate render pass if the swap chain image format changed
  if (swapChainImageFormat != oldFormat) {
    imguiManager.reset();
    vkDestroyRenderPass(device, renderPass, nullptr);
  }

  createImageViews();
  createDepthResources();

  if (swapChainImageFormat != oldFormat) {
    createRenderPass();
  }

  createFramebuffers();

  // Recreate ImGui if the render pass was rebuilt
  if (swapChainImageFormat != oldFormat) {
    createImGui();
  }

  // Recreate per-image render-finished semaphores for the new swap chain
  renderFinishedSemaphores.assign(swapChainImages.size(), VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo{};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  for (size_t i = 0; i < swapChainImages.size(); i++) {
    if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                          &renderFinishedSemaphores[i]) != VK_SUCCESS) {
      throw std::runtime_error("Failed to create render finished semaphore");
    }
  }
}

void VulkanRenderer::cleanup() {
  waitIdle();

  imguiManager.reset();

  for (auto &semaphore : renderFinishedSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, semaphore, nullptr);
    }
  }

  for (auto semaphore : imageAvailableSemaphores) {
    if (semaphore != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, semaphore, nullptr);
    }
  }
  for (auto fence : inFlightFences) {
    if (fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, fence, nullptr);
    }
  }

  if (commandPool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, commandPool, nullptr);
    commandPool = VK_NULL_HANDLE;
  }

  cleanupSwapChain();

  if (swapChain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapChain, nullptr);
    swapChain = VK_NULL_HANDLE;
  }

  if (renderPass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, renderPass, nullptr);
    renderPass = VK_NULL_HANDLE;
  }

  savePipelineCache();
  if (pipelineCache != VK_NULL_HANDLE) {
    vkDestroyPipelineCache(device, pipelineCache, nullptr);
    pipelineCache = VK_NULL_HANDLE;
  }

  if (allocator != VK_NULL_HANDLE) {
    vmaDestroyAllocator(allocator);
    allocator = VK_NULL_HANDLE;
  }

  if (device != VK_NULL_HANDLE) {
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }

  if (enableValidationLayers && debugMessenger != VK_NULL_HANDLE) {
    destroyDebugUtilsMessengerEXT(instance, debugMessenger);
    debugMessenger = VK_NULL_HANDLE;
  }

  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }

  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}
