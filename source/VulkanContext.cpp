#include "VulkanContext.h"

#include <vk_mem_alloc.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#include <process.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include "VulkanSelectors.h"
#include "VulkanUtils.h"

using vkutil::querySwapChainSupport;
using vkutil::vkCheck;

namespace {

// Per-user cache directory for the pipeline cache file, namespaced by app name
// so apps built on this library don't collide on one shared file. Falls back to
// CWD only when no platform location can be determined.
auto pipelineCachePath(const std::string &appName) -> std::filesystem::path {
  namespace fs = std::filesystem;
#if defined(_WIN32)
  if (const char *localAppData = std::getenv("LOCALAPPDATA")) {
    return fs::path(localAppData) / appName / "pipeline_cache.bin";
  }
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / "Library" / "Caches" / appName /
           "pipeline_cache.bin";
  }
#else
  if (const char *xdg = std::getenv("XDG_CACHE_HOME"); xdg && xdg[0] != '\0') {
    return fs::path(xdg) / appName / "pipeline_cache.bin";
  }
  if (const char *home = std::getenv("HOME")) {
    return fs::path(home) / ".cache" / appName / "pipeline_cache.bin";
  }
#endif
  return {"pipeline_cache.bin"};
}

// Remove abandoned pipeline-cache temp files (the "<name>.tmp.<pid>" siblings
// savePipelineCache writes then renames). A process killed between write and
// rename leaves one behind. Only sweep temps older than a minute so a
// concurrently-running instance mid-write isn't disturbed — a real write
// completes in milliseconds. Best-effort: all errors are swallowed.
void sweepStalePipelineCacheTemps(const std::filesystem::path &cacheFile) {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path dir = cacheFile.parent_path();
  if (dir.empty()) return;
  const std::string prefix = cacheFile.filename().string() + ".tmp.";

  const auto now = fs::file_time_type::clock::now();
  constexpr auto staleAfter = std::chrono::minutes(1);

  for (fs::directory_iterator it(dir, ec), end; !ec && it != end;
       it.increment(ec)) {
    const std::string filename = it->path().filename().string();
    if (!filename.starts_with(prefix)) continue;  // not one of our temps

    const auto mtime = fs::last_write_time(it->path(), ec);
    if (ec) {
      ec.clear();
      continue;
    }
    if (now - mtime < staleAfter) continue;  // recent — possibly a live writer

    fs::remove(it->path(), ec);
    ec.clear();
  }
}

auto createDebugUtilsMessengerEXT(
    VkInstance instance, const VkDebugUtilsMessengerCreateInfoEXT *pCreateInfo,
    VkDebugUtilsMessengerEXT *pDebugMessenger) -> VkResult {
  auto func = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    return func(instance, pCreateInfo, nullptr, pDebugMessenger);
  }
  return VK_ERROR_EXTENSION_NOT_PRESENT;
}

void destroyDebugUtilsMessengerEXT(VkInstance instance,
                                   VkDebugUtilsMessengerEXT debugMessenger) {
  auto func = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
      vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
  if (func != nullptr) {
    func(instance, debugMessenger, nullptr);
  }
}

// Vulkan ABI callback — VKAPI_ATTR/VKAPI_CALL don't combine cleanly with
// trailing-return syntax, so suppress the modernize check on the signature.
VKAPI_ATTR VkBool32 VKAPI_CALL
// NOLINTNEXTLINE(modernize-use-trailing-return-type)
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT /*messageType*/,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void * /*pUserData*/) {
  if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    std::cerr << "Validation layer: " << pCallbackData->pMessage << '\n';
  }

  // Opt-in for headless / CI runs: abort the process on any error-severity
  // message (real spec violations and synchronization-validation hazards) so a
  // validation failure surfaces as a non-zero exit. Best-practices and other
  // warnings are WARNING severity and never abort. Resolved once; disabled
  // unless VULKANWINDOW_VALIDATION_ABORT is set to a non-empty, non-"0" value.
  static const bool abortOnError = []() -> bool {
    const char *env = std::getenv("VULKANWINDOW_VALIDATION_ABORT");
    return env != nullptr && env[0] != '\0' && env[0] != '0';
  }();
  if (abortOnError &&
      (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
    std::cerr << "Aborting: Vulkan validation error and "
                 "VULKANWINDOW_VALIDATION_ABORT is set.\n";
    std::abort();
  }

  return VK_FALSE;
}

void fillDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &info) {
  info = {};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  // Verbose/info aren't requested — debugCallback only prints >= warning.
  info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                     VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  info.pfnUserCallback = debugCallback;
}

}  // namespace

VulkanContext::VulkanContext(GLFWwindow *window,
                             bool enableBestPracticesValidation,
                             std::string_view appName)
    : cacheName(vkutil::sanitizeCacheName(appName)), applicationName(appName) {
  try {
    createInstance(enableBestPracticesValidation);
    setupDebugMessenger();
    createSurface(window);
    pickPhysicalDevice();
    createLogicalDevice();
    createAllocator();
    createPipelineCache();
  } catch (...) {
    cleanup();
    throw;
  }
}

VulkanContext::~VulkanContext() { cleanup(); }

auto VulkanContext::checkValidationLayerSupport() const -> bool {
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

void VulkanContext::createInstance(bool enableBestPracticesValidationArg) {
  // Decide once whether validation will actually be used. If a Debug build
  // requested it but the system doesn't have VK_LAYER_KHRONOS_validation
  // installed, warn and continue without it instead of fatally exiting.
  validationLayersActive = enableValidationLayers;
  if (enableValidationLayers && !checkValidationLayerSupport()) {
    std::cerr << "Warning: VK_LAYER_KHRONOS_validation not available; "
                 "running without validation. Install your distro's Vulkan "
                 "validation-layer package to enable it.\n";
    validationLayersActive = false;
  }

  // VK_EXT_validation_features is provided by the validation layer. Per spec,
  // chaining VkValidationFeaturesEXT into pNext requires the extension to be
  // explicitly enabled. Probe the layer's extensions once.
  validationFeaturesAvailable = false;
  if (validationLayersActive) {
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(validationLayers[0], &extCount,
                                           nullptr);
    std::vector<VkExtensionProperties> layerExts(extCount);
    vkEnumerateInstanceExtensionProperties(validationLayers[0], &extCount,
                                           layerExts.data());
    for (const auto &e : layerExts) {
      if (std::string_view(e.extensionName) ==
          VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) {
        validationFeaturesAvailable = true;
        break;
      }
    }
    if (!validationFeaturesAvailable) {
      // Both synchronization validation (always enabled when available) and
      // best-practices (compile-time opt-in) are routed through
      // VK_EXT_validation_features. If the layer doesn't expose it, both are
      // silently dropped — warn so the loss is visible at startup.
      std::cerr
          << "Warning: VK_EXT_validation_features not exposed by the installed "
             "validation layer; synchronization validation"
          << (enableBestPracticesValidationArg ? " and best-practices validation"
                                               : "")
          << " will not be enabled.\n";
    }
  }

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  // Surface the consumer's app name to the driver (applicationName is the
  // unsanitised constructor argument, stable for this object's lifetime).
  appInfo.pApplicationName = applicationName.c_str();
  appInfo.applicationVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);
  appInfo.pEngineName = "VulkanWindow";
  appInfo.engineVersion = VK_MAKE_API_VERSION(0, 1, 0, 0);

  // Request the highest API the loader supports, capped at 1.3. On a Vulkan
  // 1.0 loader vkEnumerateInstanceVersion isn't exported, so fall back to 1.0.
  uint32_t loaderVersion = VK_API_VERSION_1_0;
  auto enumerateInstanceVersion =
      reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
          vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
  if (enumerateInstanceVersion != nullptr) {
    enumerateInstanceVersion(&loaderVersion);
  }
  appInfo.apiVersion = std::min(loaderVersion, VK_API_VERSION_1_3);
  instanceApiVersion = appInfo.apiVersion;

  // vkGetPhysicalDeviceFeatures2 is core in 1.1+. On a 1.0 instance, the
  // entry point isn't exported — but VK_KHR_get_physical_device_properties2
  // provides the vkGetPhysicalDeviceFeatures2KHR alias. Probe for it so we
  // don't have to silently disable 1.2/1.3 feature detection on legacy
  // loaders.
  getPhysicalDeviceFeatures2Available =
      instanceApiVersion >= VK_API_VERSION_1_1;
  needKHRFeatures2Loader = false;
  if (!getPhysicalDeviceFeatures2Available) {
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> instanceExts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount,
                                           instanceExts.data());
    for (const auto &e : instanceExts) {
      if (std::string_view(e.extensionName) ==
          VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME) {
        getPhysicalDeviceFeatures2Available = true;
        needKHRFeatures2Loader = true;
        break;
      }
    }
  }

  // Probe for VK_KHR_portability_enumeration. A modern loader hides physical
  // devices that only implement the portability subset (notably MoltenVK on
  // macOS) unless this extension is enabled and the ENUMERATE_PORTABILITY flag
  // is set below. Not platform-gated: any portability ICD benefits, and the
  // extension simply won't be present on loaders that don't need it.
  portabilityEnumerationEnabled = false;
  {
    uint32_t extCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> instanceExts(extCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extCount,
                                           instanceExts.data());
    for (const auto &e : instanceExts) {
      if (std::string_view(e.extensionName) ==
          VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME) {
        portabilityEnumerationEnabled = true;
        break;
      }
    }
  }

  VkInstanceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;
  if (portabilityEnumerationEnabled) {
    createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
  }

  auto extensions = getRequiredExtensions();
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  // Setup debug messenger info for instance creation/destruction
  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo{};
  VkValidationFeaturesEXT validationFeatures{};
  std::vector<VkValidationFeatureEnableEXT> enabledValidationFeatures;

  if (validationLayersActive) {
    enabledValidationFeatures.push_back(
        VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT);
    if (enableBestPracticesValidationArg) {
      enabledValidationFeatures.push_back(
          VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT);
    }

    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    // Enable debug messenger during instance creation/destruction.
    fillDebugMessengerCreateInfo(debugCreateInfo);

    // Enable best practices and synchronization validation. Only chain
    // VkValidationFeaturesEXT when the extension is actually enabled —
    // otherwise the loader will reject the unknown structure in pNext.
    if (validationFeaturesAvailable) {
      validationFeatures.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
      validationFeatures.enabledValidationFeatureCount =
          static_cast<uint32_t>(enabledValidationFeatures.size());
      validationFeatures.pEnabledValidationFeatures =
          enabledValidationFeatures.data();
      validationFeatures.pNext = &debugCreateInfo;
      createInfo.pNext = &validationFeatures;
    } else {
      createInfo.pNext = &debugCreateInfo;
    }
  } else {
    createInfo.enabledLayerCount = 0;
    createInfo.pNext = nullptr;
  }

  vkCheck(vkCreateInstance(&createInfo, nullptr, &instance),
          "Failed to create Vulkan instance");
}

void VulkanContext::setupDebugMessenger() {
  if (!validationLayersActive) return;

  VkDebugUtilsMessengerCreateInfoEXT createInfo{};
  fillDebugMessengerCreateInfo(createInfo);

  vkCheck(createDebugUtilsMessengerEXT(instance, &createInfo, &debugMessenger),
          "Failed to set up debug messenger");
}

void VulkanContext::createSurface(GLFWwindow *window) {
  vkCheck(glfwCreateWindowSurface(instance, window, nullptr, &surface),
          "Failed to create window surface");
}

void VulkanContext::pickPhysicalDevice() {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

  if (deviceCount == 0) {
    throw std::runtime_error("Failed to find GPUs with Vulkan support");
  }

  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

  // Score all suitable devices and pick the best one. An optional
  // VULKANWINDOW_DEVICE_INDEX env var forces a specific enumeration index
  // (useful for testing); otherwise we use strict >  comparison so ties go
  // to the lower enumeration index — driver enumeration order is stable
  // across runs on a given system, making selection reproducible.
  int forcedIndex = -1;
  if (const char *forced = std::getenv("VULKANWINDOW_DEVICE_INDEX");
      forced && forced[0] != '\0') {
    try {
      forcedIndex = std::stoi(forced);
    } catch (...) {
      forcedIndex = -1;
    }
  }

  if (forcedIndex >= 0 && std::cmp_less(forcedIndex, devices.size())) {
    if (isDeviceSuitable(devices[forcedIndex])) {
      physicalDevice = devices[forcedIndex];
    }
  } else {
    int bestScore = -1;
    for (const auto &dev : devices) {
      if (!isDeviceSuitable(dev)) continue;

      int score = rateDeviceSuitability(dev);
      if (score > bestScore) {
        bestScore = score;
        physicalDevice = dev;
      }
    }
  }

  if (physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("Failed to find a suitable GPU");
  }

  // Cache queue families now so createLogicalDevice / createSwapChain don't
  // repeat the enumeration. Suitability already confirmed both are present.
  const vkutil::QueueFamilyIndices indices =
      vkutil::findQueueFamilies(physicalDevice, surface);
  graphicsQueueFamily = indices.graphicsFamily.value();
  presentQueueFamily = indices.presentFamily.value();

  // Cache the properties of the selected device — they're queried in several
  // places (logical-device creation, pipeline-cache header validation).
  vkGetPhysicalDeviceProperties(physicalDevice, &physicalDeviceProperties);
  std::cout << "Selected GPU: " << physicalDeviceProperties.deviceName
            << " (Vulkan "
            << VK_API_VERSION_MAJOR(physicalDeviceProperties.apiVersion) << "."
            << VK_API_VERSION_MINOR(physicalDeviceProperties.apiVersion) << "."
            << VK_API_VERSION_PATCH(physicalDeviceProperties.apiVersion)
            << ")\n";
}

void VulkanContext::createLogicalDevice() {
  // Queue families were cached in pickPhysicalDevice — no need to re-query.
  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {graphicsQueueFamily,
                                            presentQueueFamily};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  // Use the cached device properties (populated in pickPhysicalDevice) to
  // conditionally enable 1.2 features.
  deviceApiVersion = physicalDeviceProperties.apiVersion;

  // features2 needs both the entry point reachable (1.1+ core OR the KHR
  // alias from VK_KHR_get_physical_device_properties2 on a 1.0 instance,
  // both flagged by getPhysicalDeviceFeatures2Available) AND a 1.1+ device
  // that knows how to fill in the struct.
  bool useFeatures2 = getPhysicalDeviceFeatures2Available &&
                      deviceApiVersion >= VK_API_VERSION_1_1;

  // Always resolve via vkGetInstanceProcAddr instead of relying on the
  // statically-linked symbol — that way a truly-1.0 loader (no
  // vkGetPhysicalDeviceFeatures2 export at all) doesn't fail program load on
  // an unresolved symbol. The core and KHR symbols share an ABI.
  PFN_vkGetPhysicalDeviceFeatures2 getFeatures2Fn = nullptr;
  if (useFeatures2) {
    const char *symbol = needKHRFeatures2Loader
                             ? "vkGetPhysicalDeviceFeatures2KHR"
                             : "vkGetPhysicalDeviceFeatures2";
    getFeatures2Fn = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, symbol));
    if (getFeatures2Fn == nullptr) {
      useFeatures2 = false;
    }
  }

  // The renderer currently uses the legacy render-pass + VkSubmitInfo path,
  // so most 1.3 features stay disabled. The exception is
  // pipelineCreationCacheControl, which lets us pass
  // VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT to skip internal
  // pipeline-cache locking on the render thread.
  VkPhysicalDeviceVulkan12Features features12{};
  features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;

  VkPhysicalDeviceVulkan13Features features13{};
  features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;

  // VkPhysicalDeviceVulkan12Features may only be chained when the device is
  // 1.2+; VkPhysicalDeviceVulkan13Features needs 1.3+. Chaining a struct newer
  // than the device's API version is a spec violation (and warns under
  // validation), so gate each one. deviceIs13 implies deviceIs12.
  const bool deviceIs12 = deviceApiVersion >= VK_API_VERSION_1_2 &&
                          instanceApiVersion >= VK_API_VERSION_1_2;
  const bool deviceIs13 = deviceApiVersion >= VK_API_VERSION_1_3 &&
                          instanceApiVersion >= VK_API_VERSION_1_3;
  if (deviceIs13) {
    features12.pNext = &features13;
  }

  VkPhysicalDeviceFeatures2 features2{};
  features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
  if (deviceIs12) {
    features2.pNext = &features12;
  }

  // Querying the 1.2/1.3 feature structs is only valid on a matching device.
  if (useFeatures2 && deviceIs12) {
    VkPhysicalDeviceVulkan12Features supported12{};
    supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    VkPhysicalDeviceVulkan13Features supported13{};
    supported13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    if (deviceIs13) {
      supported12.pNext = &supported13;
    }

    VkPhysicalDeviceFeatures2 supportedFeatures2{};
    supportedFeatures2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    supportedFeatures2.pNext = &supported12;
    getFeatures2Fn(physicalDevice, &supportedFeatures2);

    features12.imagelessFramebuffer = supported12.imagelessFramebuffer;
    if (deviceIs13) {
      features13.pipelineCreationCacheControl =
          supported13.pipelineCreationCacheControl;
      pipelineCacheExternallySyncSupported =
          supported13.pipelineCreationCacheControl == VK_TRUE;
    }
  }

  VkDeviceCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();
  if (useFeatures2) {
    createInfo.pNext = &features2;
  }
  // Build the enabled device-extension list. The Vulkan spec mandates that if a
  // physical device exposes VK_KHR_portability_subset it MUST be enabled — this
  // is how portability implementations (e.g. MoltenVK) advertise themselves.
  // The name is a beta-gated macro (VK_ENABLE_BETA_EXTENSIONS), so use the
  // string literal to avoid pulling in the provisional headers.
  std::vector<const char *> enabledDeviceExtensions(deviceExtensions.begin(),
                                                    deviceExtensions.end());
  {
    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount,
                                         nullptr);
    std::vector<VkExtensionProperties> available(extCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extCount,
                                         available.data());
    for (const auto &e : available) {
      if (std::string_view(e.extensionName) == "VK_KHR_portability_subset") {
        enabledDeviceExtensions.push_back("VK_KHR_portability_subset");
        break;
      }
    }
  }

  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(enabledDeviceExtensions.size());
  createInfo.ppEnabledExtensionNames = enabledDeviceExtensions.data();

  // Device layers have been deprecated since Vulkan 1.0 — the loader ignores
  // them and only instance layers (set in createInstance) take effect. Setting
  // enabledLayerCount here triggers a validation warning, so we leave it at the
  // zero-initialized default.

  vkCheck(vkCreateDevice(physicalDevice, &createInfo, nullptr, &device),
          "Failed to create logical device");

  vkGetDeviceQueue(device, graphicsQueueFamily, 0, &graphicsQueue);
  vkGetDeviceQueue(device, presentQueueFamily, 0, &presentQueue);

  useImagelessFramebuffer = (features12.imagelessFramebuffer == VK_TRUE);
}

void VulkanContext::createAllocator() {
  VmaAllocatorCreateInfo allocatorInfo{};
  allocatorInfo.instance = instance;
  allocatorInfo.physicalDevice = physicalDevice;
  allocatorInfo.device = device;
  // VMA looks up entry points against the instance's enabled API version;
  // using the device's reported version could probe for functions the
  // instance never loaded.
  allocatorInfo.vulkanApiVersion =
      std::min(instanceApiVersion, deviceApiVersion);

  vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator),
          "Failed to create VMA allocator");
}

void VulkanContext::createPipelineCache() {
  VkPipelineCacheCreateInfo cacheInfo{};
  cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
  // The pipeline cache is only ever touched from the main render thread —
  // tell the driver to skip internal locking. The flag requires the 1.3
  // pipelineCreationCacheControl feature; we only set it when that feature
  // was enabled in createLogicalDevice.
  if (pipelineCacheExternallySyncSupported) {
    cacheInfo.flags = VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT;
  }

  // Try to load existing cache from disk
  std::vector<char> cacheData;
  const std::filesystem::path cachePath = pipelineCachePath(cacheName);
  // Clear out any temp files left by a process killed mid-save (see
  // savePipelineCache's temp→rename dance).
  sweepStalePipelineCacheTemps(cachePath);
  std::ifstream cacheFile(cachePath, std::ios::binary | std::ios::ate);
  if (cacheFile.is_open()) {
    auto size = cacheFile.tellg();
    if (size > 0) {
      cacheData.resize(static_cast<size_t>(size));
      cacheFile.seekg(0);
      cacheFile.read(cacheData.data(), size);
    }
    cacheFile.close();

    // Validate cache header against current device. Per Vulkan spec the
    // header layout is:
    //   uint32 headerLength
    //   uint32 headerVersion
    //   uint32 vendorID
    //   uint32 deviceID
    //   uint8  pipelineCacheUUID[VK_UUID_SIZE]
    constexpr size_t kHeaderVersionOffset = sizeof(uint32_t);
    constexpr size_t kVendorIDOffset = kHeaderVersionOffset + sizeof(uint32_t);
    constexpr size_t kDeviceIDOffset = kVendorIDOffset + sizeof(uint32_t);
    constexpr size_t kUUIDOffset = kDeviceIDOffset + sizeof(uint32_t);
    constexpr size_t kHeaderSize = kUUIDOffset + VK_UUID_SIZE;

    if (cacheData.size() >= kHeaderSize) {
      uint32_t headerLength = 0;
      uint32_t headerVersion = 0;
      uint32_t vendorID = 0;
      uint32_t deviceID = 0;
      std::memcpy(&headerLength, cacheData.data(), sizeof(uint32_t));
      std::memcpy(&headerVersion, cacheData.data() + kHeaderVersionOffset,
                  sizeof(uint32_t));
      std::memcpy(&vendorID, cacheData.data() + kVendorIDOffset,
                  sizeof(uint32_t));
      std::memcpy(&deviceID, cacheData.data() + kDeviceIDOffset,
                  sizeof(uint32_t));

      if (headerLength == kHeaderSize &&
          headerVersion == VK_PIPELINE_CACHE_HEADER_VERSION_ONE &&
          vendorID == physicalDeviceProperties.vendorID &&
          deviceID == physicalDeviceProperties.deviceID &&
          std::memcmp(cacheData.data() + kUUIDOffset,
                      physicalDeviceProperties.pipelineCacheUUID,
                      VK_UUID_SIZE) == 0) {
        cacheInfo.initialDataSize = cacheData.size();
        cacheInfo.pInitialData = cacheData.data();
      }
    }
  }

  if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) !=
      VK_SUCCESS) {
    // Non-fatal: create an empty cache if loading failed (corrupt cache data
    // that passed our header check is the most likely cause).
    cacheInfo.initialDataSize = 0;
    cacheInfo.pInitialData = nullptr;
    if (vkCreatePipelineCache(device, &cacheInfo, nullptr, &pipelineCache) !=
        VK_SUCCESS) {
      std::cerr << "Warning: failed to create pipeline cache; continuing "
                   "without cache.\n";
      pipelineCache = VK_NULL_HANDLE;
    }
  }
}

void VulkanContext::savePipelineCache() const {
  if (pipelineCache == VK_NULL_HANDLE) return;

  size_t cacheSize = 0;
  // On VK_ERROR_DEVICE_LOST (or any other failure) the size is undefined and
  // the data we'd read out is meaningless — skip persistence rather than write
  // garbage that fails the header check on next start.
  if (vkGetPipelineCacheData(device, pipelineCache, &cacheSize, nullptr) !=
      VK_SUCCESS) {
    return;
  }

  if (cacheSize == 0) return;

  std::vector<char> cacheData(cacheSize);
  if (vkGetPipelineCacheData(device, pipelineCache, &cacheSize,
                             cacheData.data()) != VK_SUCCESS) {
    return;
  }

  const std::filesystem::path cachePath = pipelineCachePath(cacheName);
  std::error_code ec;
  std::filesystem::create_directories(cachePath.parent_path(), ec);
  // Ignore directory-creation errors — opening the file below will fail
  // gracefully and we silently skip persistence.

  // Write to a sibling temp file then rename, so a kill mid-write can't
  // leave a truncated cache that fails the header check on next start.
  // PID-suffix the temp name so two concurrent processes don't clobber each
  // other's in-progress writes.
#if defined(_WIN32)
  const auto pid = static_cast<int64_t>(_getpid());
#else
  const auto pid = static_cast<int64_t>(::getpid());
#endif
  std::filesystem::path tmpPath = cachePath;
  tmpPath += ".tmp." + std::to_string(pid);

  {
    std::ofstream cacheFile(tmpPath, std::ios::binary | std::ios::trunc);
    if (!cacheFile.is_open()) return;
    cacheFile.write(cacheData.data(), static_cast<std::streamsize>(cacheSize));
    if (!cacheFile.good()) {
      cacheFile.close();
      std::filesystem::remove(tmpPath, ec);
      return;
    }
  }

  // Sync data to disk before rename so a power loss between rename and the
  // kernel flushing dirty pages can't leave a torn cache. fsync requires a
  // writable fd on some platforms, hence O_WRONLY. Also fsync the parent
  // directory after the rename so the directory entry is durable. On Windows
  // the CRT flushes on close, so this is a no-op there.
#if !defined(_WIN32)
  if (int fd = ::open(tmpPath.c_str(), O_WRONLY); fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
#endif

  std::filesystem::rename(tmpPath, cachePath, ec);
  if (ec) {
    std::filesystem::remove(tmpPath, ec);
    return;
  }

#if !defined(_WIN32)
  if (int fd = ::open(cachePath.parent_path().c_str(), O_RDONLY); fd >= 0) {
    ::fsync(fd);
    ::close(fd);
  }
#endif
}

auto VulkanContext::isDeviceSuitable(VkPhysicalDevice physDevice) const
    -> bool {
  vkutil::QueueFamilyIndices indices =
      vkutil::findQueueFamilies(physDevice, surface);

  bool extensionsSupported = checkDeviceExtensionSupport(physDevice);

  bool swapChainAdequate = false;

  if (extensionsSupported) {
    vkutil::SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(physDevice, surface);
    swapChainAdequate = !swapChainSupport.formats.empty() &&
                        !swapChainSupport.presentModes.empty();
  }

  return indices.isComplete() && extensionsSupported && swapChainAdequate;
}

auto VulkanContext::checkDeviceExtensionSupport(
    VkPhysicalDevice physDevice) const -> bool {
  uint32_t extensionCount = 0;
  vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount,
                                       nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(physDevice, nullptr, &extensionCount,
                                       availableExtensions.data());

  std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                           deviceExtensions.end());

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

auto VulkanContext::getRequiredExtensions() const
    -> std::vector<const char *> {
  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions =
      glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

  if (validationLayersActive) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (validationFeaturesAvailable) {
      extensions.push_back(VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME);
    }
  }

  // 1.0 loader path: enable the KHR alias so vkGetPhysicalDeviceFeatures2KHR
  // is reachable. On a 1.1+ instance the core symbol is already exported and
  // the extension must not be enabled (it's promoted to core).
  if (needKHRFeatures2Loader) {
    extensions.push_back(
        VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
  }

  // Required alongside the ENUMERATE_PORTABILITY instance flag so portability
  // ICDs (e.g. MoltenVK) are enumerated. Probed in createInstance.
  if (portabilityEnumerationEnabled) {
    extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
  }

  return extensions;
}

auto VulkanContext::rateDeviceSuitability(VkPhysicalDevice physDevice) -> int {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physDevice, &props);

  VkPhysicalDeviceMemoryProperties memProps;
  vkGetPhysicalDeviceMemoryProperties(physDevice, &memProps);

  int score = 0;

  // Device-type bonus dominates over heap size and API version so a discrete
  // GPU always outranks an integrated one even if the iGPU has more reported
  // device-local memory (e.g. UMA designs, ReBAR exposing all of system RAM).
  constexpr int kDiscreteBonus = 1'000'000;
  constexpr int kIntegratedBonus = 100'000;
  if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
    score += kDiscreteBonus;
  } else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
    score += kIntegratedBonus;
  }

  // Score by largest device-local heap (in MB). Summing all device-local
  // heaps double-counts ReBAR/host-coherent overlays that mirror VRAM.
  VkDeviceSize largestDeviceLocalHeap = 0;
  for (uint32_t i = 0; i < memProps.memoryHeapCount; i++) {
    if ((memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) &&
        memProps.memoryHeaps[i].size > largestDeviceLocalHeap) {
      largestDeviceLocalHeap = memProps.memoryHeaps[i].size;
    }
  }
  // Cap the heap term so a huge-iGPU heap can't approach the
  // integrated/discrete gap. 64 GiB caps at ~65 000, well below the 900 000 gap
  // above.
  constexpr VkDeviceSize kHeapCapBytes = VkDeviceSize{64} * 1024 * 1024 * 1024;
  const VkDeviceSize cappedHeap =
      std::min(largestDeviceLocalHeap, kHeapCapBytes);
  score += static_cast<int>(cappedHeap / (1024 * 1024));

  // Prefer higher Vulkan API version support
  score += static_cast<int>(VK_API_VERSION_MINOR(props.apiVersion)) * 100;

  return score;
}

void VulkanContext::cleanup() {
  // Persist the pipeline cache before the device goes away.
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

  if (validationLayersActive && debugMessenger != VK_NULL_HANDLE) {
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
