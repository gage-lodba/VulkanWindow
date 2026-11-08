#pragma once

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace vkutil {

/// Translate VkResult to its symbolic string. Covers every result value
/// defined by the Vulkan headers the mapping was generated from
/// (VK_HEADER_VERSION 363: 50 distinct values) and is guarded by
/// VK_HEADER_VERSION so it still compiles against, and maps every value of,
/// older headers back to Vulkan 1.3.204 (38 values; 1.3.275 has 47). Returns
/// "VK_RESULT_<unknown>" only for result codes introduced after the mapping was
/// last extended; the unit test's header-version static_assert flags a Vulkan
/// headers bump so the mapping can be updated. The returned strings are the
/// current (Vulkan 1.4) canonical spellings; aliases share one numeric value
/// and therefore one string.
auto vkResultString(VkResult r) -> const char*;

/// Throw std::runtime_error if `r != VK_SUCCESS`, formatted as
/// "<what>: <vkResultString(r)>".
void vkCheck(VkResult r, const char* what);

/// Create a VkShaderModule from the given SPIR-V bytecode. `code` must be
/// aligned to a 4-byte boundary and point at `sizeBytes` bytes.
///
/// Throws std::invalid_argument when `code` is null, `sizeBytes` is 0, or
/// `sizeBytes` is not a multiple of sizeof(uint32_t) (the Vulkan-required
/// shape of VkShaderModuleCreateInfo). Throws std::runtime_error if
/// vkCreateShaderModule fails; destroy the module with vkDestroyShaderModule.
[[nodiscard]] auto createShaderModule(VkDevice device, const uint32_t* code,
                                      size_t sizeBytes) -> VkShaderModule;

/// Convert one sRGB-encoded colour channel in [0, 1] to its linear value via
/// the standard sRGB transfer function. Alpha is not gamma-encoded and should
/// not be passed through this. Useful when rendering ImGui onto an sRGB
/// surface (see SurfaceFormatPreference::Srgb): linearise your ImGuiCol_*
/// values so the GPU's linear→sRGB encode reproduces the authored colour.
auto srgbToLinear(float channel) -> float;

/// True for every 8-bit sRGB format — the encodings where the GPU
/// gamma-encodes colour on store: R8_SRGB, R8G8_SRGB, R8G8B8_SRGB,
/// B8G8R8_SRGB, R8G8B8A8_SRGB, B8G8R8A8_SRGB and A8B8G8R8_SRGB_PACK32. Used to
/// decide whether ImGui's sRGB-authored colours need linearising for the
/// chosen swap-chain format.
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
///
/// `surface` must be a valid VkSurfaceKHR: VK_NULL_HANDLE (or a surface not
/// supported by `device`) is invalid Vulkan usage for
/// vkGetPhysicalDeviceSurfaceSupportKHR and is not tolerated here.
auto findQueueFamilies(VkPhysicalDevice device,
                       VkSurfaceKHR surface) -> QueueFamilyIndices;

/// Query the swap-chain support for a (physical device, surface) pair.
///
/// The VkResult of the three surface queries is deliberately not propagated
/// (the struct is plain data): on failure or for a surface with no presentation
/// engine, `capabilities` stays zero-initialised and the format/present-mode
/// vectors are empty. Callers must treat that as "not usable" rather than
/// assuming the surface supports presentation.
auto querySwapChainSupport(VkPhysicalDevice device,
                           VkSurfaceKHR surface) -> SwapChainSupportDetails;

}  // namespace vkutil
