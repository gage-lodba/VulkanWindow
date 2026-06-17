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

/// Convert one sRGB-encoded colour channel in [0, 1] to its linear value via
/// the standard sRGB transfer function. Alpha is not gamma-encoded and should
/// not be passed through this. Useful when rendering ImGui onto an sRGB
/// surface (see SurfaceFormatPreference::Srgb): linearise your ImGuiCol_*
/// values so the GPU's linear→sRGB encode reproduces the authored colour.
auto srgbToLinear(float channel) -> float;

/// True for the 8-bit sRGB surface formats — the encodings where the GPU
/// gamma-encodes colour on store. Used to decide whether ImGui's sRGB-authored
/// colours need linearising for the chosen swap-chain format.
auto isSrgbFormat(VkFormat format) -> bool;

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
auto findQueueFamilies(VkPhysicalDevice device,
                       VkSurfaceKHR surface) -> QueueFamilyIndices;

/// Query the swap-chain support for a (physical device, surface) pair.
auto querySwapChainSupport(VkPhysicalDevice device,
                           VkSurfaceKHR surface) -> SwapChainSupportDetails;

}  // namespace vkutil
