#include "VulkanUtils.h"

#include <cmath>
#include <stdexcept>
#include <string>

namespace vkutil {

auto vkResultString(VkResult r) -> const char* {
  // The mapping was generated from VK_HEADER_VERSION 363 and covers every
  // distinct VkResult value it defines (50). It is guarded by VK_HEADER_VERSION
  // so it also compiles against older headers, mapping every value they define
  // (e.g. 47 with the 1.3.275 headers the CI image ships). Values introduced
  // after 1.4.363 cannot be named here and hit `default`.
  //
  // Returned strings are the current (Vulkan 1.4) canonical spellings. Three
  // values have canonical names that do not exist in the 1.3.275 headers, so
  // their `case` labels use the alias spelling those headers do define (same
  // numeric value); the inline comment names the canonical shape.
  // Values that only exist in newer headers are guarded by VK_HEADER_VERSION
  // (threshold = first header version defining them), so the translation unit
  // still compiles against the oldest Vulkan 1.3 headers the project supports
  // (1.3.204):
  //   >= 213: VK_ERROR_COMPRESSION_EXHAUSTED_EXT
  //   >= 238: the six VK_KHR_video_queue/session result codes
  //   >= 246: VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT (legacy alias label)
  //   >= 274: VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR
  //   >= 294: VK_PIPELINE_BINARY_MISSING_KHR, VK_ERROR_NOT_ENOUGH_SPACE_KHR
  //   >= 335: VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT
  switch (r) {
    case VK_SUCCESS:
      return "VK_SUCCESS";
    case VK_NOT_READY:
      return "VK_NOT_READY";
    case VK_TIMEOUT:
      return "VK_TIMEOUT";
    case VK_EVENT_SET:
      return "VK_EVENT_SET";
    case VK_EVENT_RESET:
      return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
      return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
      return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
      return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
      return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
      return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_UNKNOWN:
      return "VK_ERROR_UNKNOWN";
    // VK_ERROR_VALIDATION_FAILED is the current canonical spelling; the _EXT
    // alias is the only spelling defined by the 1.3.275 headers.
    case VK_ERROR_VALIDATION_FAILED_EXT:
      return "VK_ERROR_VALIDATION_FAILED";
    case VK_ERROR_OUT_OF_POOL_MEMORY:
      return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_INVALID_EXTERNAL_HANDLE:
      return "VK_ERROR_INVALID_EXTERNAL_HANDLE";
    case VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS:
      return "VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS";
    case VK_ERROR_FRAGMENTATION:
      return "VK_ERROR_FRAGMENTATION";
    case VK_PIPELINE_COMPILE_REQUIRED:
      return "VK_PIPELINE_COMPILE_REQUIRED";
    // VK_ERROR_NOT_PERMITTED is the current canonical spelling; the _KHR alias
    // is the only spelling defined by the 1.3.275 headers.
    case VK_ERROR_NOT_PERMITTED_KHR:
      return "VK_ERROR_NOT_PERMITTED";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
      return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_SUBOPTIMAL_KHR:
      return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_INCOMPATIBLE_DISPLAY_KHR:
      return "VK_ERROR_INCOMPATIBLE_DISPLAY_KHR";
    case VK_ERROR_INVALID_SHADER_NV:
      return "VK_ERROR_INVALID_SHADER_NV";
#if VK_HEADER_VERSION >= 238
    case VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR:
      return "VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR:
      return "VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR:
      return "VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR:
      return "VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR:
      return "VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR";
    case VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR:
      return "VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR";
#endif
    case VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT:
      return "VK_ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT";
#if VK_HEADER_VERSION >= 335
    case VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT:
      return "VK_ERROR_PRESENT_TIMING_QUEUE_FULL_EXT";
#endif
    case VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT:
      return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    case VK_THREAD_IDLE_KHR:
      return "VK_THREAD_IDLE_KHR";
    case VK_THREAD_DONE_KHR:
      return "VK_THREAD_DONE_KHR";
    case VK_OPERATION_DEFERRED_KHR:
      return "VK_OPERATION_DEFERRED_KHR";
    case VK_OPERATION_NOT_DEFERRED_KHR:
      return "VK_OPERATION_NOT_DEFERRED_KHR";
#if VK_HEADER_VERSION >= 274
    case VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR:
      return "VK_ERROR_INVALID_VIDEO_STD_PARAMETERS_KHR";
#endif
#if VK_HEADER_VERSION >= 213
    case VK_ERROR_COMPRESSION_EXHAUSTED_EXT:
      return "VK_ERROR_COMPRESSION_EXHAUSTED_EXT";
#endif
      // VK_INCOMPATIBLE_SHADER_BINARY_EXT is the current canonical spelling;
      // the VK_ERROR_ prefix is the legacy alias defined by the 1.3.275
      // headers.
#if VK_HEADER_VERSION >= 246
    case VK_ERROR_INCOMPATIBLE_SHADER_BINARY_EXT:
      return "VK_INCOMPATIBLE_SHADER_BINARY_EXT";
#endif
#if VK_HEADER_VERSION >= 294
    case VK_PIPELINE_BINARY_MISSING_KHR:
      return "VK_PIPELINE_BINARY_MISSING_KHR";
    case VK_ERROR_NOT_ENOUGH_SPACE_KHR:
      return "VK_ERROR_NOT_ENOUGH_SPACE_KHR";
#endif
    default:
      return "VK_RESULT_<unknown>";
  }
}

auto createShaderModule(VkDevice device, const uint32_t* code,
                        size_t sizeBytes) -> VkShaderModule {
  // Defensive checks for the Vulkan-required shape of
  // VkShaderModuleCreateInfo: code must be non-null and codeSize must be a
  // non-zero multiple of 4. Misuse throws a clear std::invalid_argument
  // instead of surfacing as a driver error or validation message.
  if (code == nullptr) {
    throw std::invalid_argument("createShaderModule: code must not be null");
  }
  if (sizeBytes == 0) {
    throw std::invalid_argument(
        "createShaderModule: sizeBytes must be greater than 0");
  }
  if (sizeBytes % sizeof(uint32_t) != 0) {
    throw std::invalid_argument(
        "createShaderModule: sizeBytes must be a multiple of sizeof(uint32_t)");
  }

  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = sizeBytes;
  info.pCode = code;

  VkShaderModule shaderModule = VK_NULL_HANDLE;
  vkCheck(vkCreateShaderModule(device, &info, nullptr, &shaderModule),
          "Failed to create shader module");
  return shaderModule;
}

void vkCheck(VkResult r, const char* what) {
  if (r != VK_SUCCESS) {
    throw std::runtime_error(std::string(what) + ": " + vkResultString(r));
  }
}

auto srgbToLinear(float channel) -> float {
  return channel <= 0.04045f ? channel / 12.92f
                             : std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

auto isSrgbFormat(VkFormat format) -> bool {
  // Every eight-bit sRGB format defined by the Vulkan headers: grey (R8),
  // two-channel (R8G8), three-channel and four-channel variants, plus the
  // packed A8B8G8R8 layout.
  switch (format) {
    case VK_FORMAT_R8_SRGB:
    case VK_FORMAT_R8G8_SRGB:
    case VK_FORMAT_R8G8B8_SRGB:
    case VK_FORMAT_B8G8R8_SRGB:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
      return true;
    default:
      return false;
  }
}

auto findQueueFamilies(VkPhysicalDevice device,
                       VkSurfaceKHR surface) -> QueueFamilyIndices {
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

auto querySwapChainSupport(VkPhysicalDevice device,
                           VkSurfaceKHR surface) -> SwapChainSupportDetails {
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

}  // namespace vkutil
