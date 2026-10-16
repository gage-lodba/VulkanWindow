#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace vkutil {

/// Translate VkResult to its symbolic string. Returns "VK_RESULT_<unknown>"
/// for codes not in the known set.
auto vkResultString(VkResult r) -> const char*;

/// Throw std::runtime_error if `r != VK_SUCCESS`, formatted as
/// "<what>: <vkResultString(r)>".
void vkCheck(VkResult r, const char* what);

/// Create a VkShaderModule from the given SPIR-V bytecode. `code` must be
/// aligned to a 4-byte boundary. Throws on failure; destroy with
/// vkDestroyShaderModule.
[[nodiscard]] auto createShaderModule(VkDevice device, const uint32_t* code,
                                      size_t sizeBytes) -> VkShaderModule;

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
  // Value-initialised on purpose: vkGetPhysicalDeviceSurfaceCapabilitiesKHR
  // may legitimately write only part of this struct (Mesa's lavapipe accepts a
  // VK_EXT_headless_surface but reports no capabilities at all, since there is
  // no presentation engine behind it). Without the initialiser the unwritten
  // members keep whatever was on the stack, so callers would read indeterminate
  // values — a self-consistency check like maxImageCount >= minImageCount then
  // fails nondeterministically instead of being skipped.
  VkSurfaceCapabilitiesKHR capabilities{};
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
