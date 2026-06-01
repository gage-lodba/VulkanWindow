#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace vkutil {

/// Translate VkResult to its symbolic string. Returns "VK_RESULT_<unknown>"
/// for codes not in the known set.
auto vkResultString(VkResult r) -> const char *;

/// Throw std::runtime_error if `r != VK_SUCCESS`, formatted as
/// "<what>: <vkResultString(r)>".
void vkCheck(VkResult r, const char *what);

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

/// Find a graphics queue family that also supports presenting to `surface`,
/// preferring a single family that supports both.
auto findQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface)
    -> QueueFamilyIndices;

/// Query the swap-chain support for a (physical device, surface) pair.
auto querySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface)
    -> SwapChainSupportDetails;

}  // namespace vkutil
