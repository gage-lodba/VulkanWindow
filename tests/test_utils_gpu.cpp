#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "VulkanUtils.h"
#include "test_fixture.h"

namespace {

/// Direct oracle query: does `queueFamily` support presenting to `surface`?
auto supportsPresent(VkPhysicalDevice physicalDevice, uint32_t queueFamily,
                     VkSurfaceKHR surface) -> bool {
  VkBool32 presentSupport = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamily, surface,
                                       &presentSupport);
  return presentSupport == VK_TRUE;
}

auto queueFamilyCount(VkPhysicalDevice physicalDevice) -> uint32_t {
  uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
  return count;
}

auto queueFamilies(VkPhysicalDevice physicalDevice)
    -> std::vector<VkQueueFamilyProperties> {
  uint32_t count = queueFamilyCount(physicalDevice);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count,
                                           families.data());
  return families;
}

/// The queue families findQueueFamilies documents it should pick: the first
/// family that is both graphics and can present (so both fields share it), or,
/// when no such family exists, the first graphics family and the first
/// present-capable family independently.
struct ExpectedQueueFamilies {
  std::optional<uint32_t> graphics;
  std::optional<uint32_t> present;
};

auto expectedQueueFamilies(VkPhysicalDevice physicalDevice,
                           VkSurfaceKHR surface) -> ExpectedQueueFamilies {
  const auto families = queueFamilies(physicalDevice);
  for (uint32_t i = 0; i < families.size(); ++i) {
    if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
        supportsPresent(physicalDevice, i, surface)) {
      return {i, i};
    }
  }

  ExpectedQueueFamilies expected;
  for (uint32_t i = 0; i < families.size(); ++i) {
    if (!expected.graphics.has_value() &&
        (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
      expected.graphics = i;
    }
    if (!expected.present.has_value() &&
        supportsPresent(physicalDevice, i, surface)) {
      expected.present = i;
    }
  }
  return expected;
}

// A minimal-but-valid SPIR-V fragment shader (`glslc --target-env=vulkan1.1`):
//   #version 450
//   layout(location = 0) out vec4 c;
//   void main() { c = vec4(1.0); }
// Real bytecode is used rather than a bare 5-word header so the driver and the
// validation layer see a module they can actually parse.
// clang-format off
constexpr uint32_t kMinimalFragmentSpirv[] = {
    0x07230203, 0x00010300, 0x000d000b, 0x0000000c, 0x00000000, 0x00020011,
    0x00000001, 0x0006000b, 0x00000001, 0x4c534c47, 0x6474732e, 0x3035342e,
    0x00000000, 0x0003000e, 0x00000000, 0x00000001, 0x0006000f, 0x00000004,
    0x00000004, 0x6e69616d, 0x00000000, 0x00000009, 0x00030010, 0x00000004,
    0x00000007, 0x00030003, 0x00000002, 0x000001c2, 0x000a0004, 0x475f4c47,
    0x4c474f4f, 0x70635f45, 0x74735f70, 0x5f656c79, 0x656e696c, 0x7269645f,
    0x69746365, 0x00006576, 0x00080004, 0x475f4c47, 0x4c474f4f, 0x6e695f45,
    0x64756c63, 0x69645f65, 0x74636572, 0x00657669, 0x00040005, 0x00000004,
    0x6e69616d, 0x00000000, 0x00030005, 0x00000009, 0x00000063, 0x00040047,
    0x00000009, 0x0000001e, 0x00000000, 0x00020013, 0x00000002, 0x00030021,
    0x00000003, 0x00000002, 0x00030016, 0x00000006, 0x00000020, 0x00040017,
    0x00000007, 0x00000006, 0x00000004, 0x00040020, 0x00000008, 0x00000003,
    0x00000007, 0x0004003b, 0x00000008, 0x00000009, 0x00000003, 0x0004002b,
    0x00000006, 0x0000000a, 0x3f800000, 0x0007002c, 0x00000007, 0x0000000b,
    0x0000000a, 0x0000000a, 0x0000000a, 0x0000000a, 0x00050036, 0x00000002,
    0x00000004, 0x00000000, 0x00000003, 0x000200f8, 0x00000005, 0x0003003e,
    0x00000009, 0x0000000b, 0x000100fd, 0x00010038};
// clang-format on

}  // namespace

// -----------------------------------------------------------------------------
// findQueueFamilies
// -----------------------------------------------------------------------------

TEST_CASE(
    "findQueueFamilies matches a direct query of graphics+present families") {
  VulkanTestFixture fx;
  if (!fx) return;
  // The helper takes a real VkSurfaceKHR: passing VK_NULL_HANDLE to
  // vkGetPhysicalDeviceSurfaceSupportKHR inside it would be invalid usage
  // (VUID-vkGetPhysicalDeviceSurfaceSupportKHR-surface-parameter), so this
  // test only runs when the fixture could create a headless surface.
  if (!fx.tryCreateHeadlessSurface()) return;

  const vkutil::QueueFamilyIndices indices =
      vkutil::findQueueFamilies(fx.physicalDevice, fx.surface);
  const ExpectedQueueFamilies expected =
      expectedQueueFamilies(fx.physicalDevice, fx.surface);

  // The exact documented preference: a family that is both graphics and
  // present when one exists, otherwise separate families.
  REQUIRE(indices.graphicsFamily.has_value());
  CHECK(indices.graphicsFamily == expected.graphics);
  CHECK(indices.presentFamily == expected.present);

  // Whatever was picked must be a real queue family with the flags it claims.
  const auto families = queueFamilies(fx.physicalDevice);
  const uint32_t graphics = *indices.graphicsFamily;
  REQUIRE(graphics < families.size());
  CHECK((families[graphics].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0);
  if (indices.presentFamily.has_value()) {
    const uint32_t present = *indices.presentFamily;
    REQUIRE(present < families.size());
    CHECK(supportsPresent(fx.physicalDevice, present, fx.surface));
  }

  // isComplete() is exactly "both are present" — it must not disagree with the
  // optional fields it is derived from.
  CHECK((indices.isComplete() == (indices.graphicsFamily.has_value() &&
                                  indices.presentFamily.has_value())));
}

TEST_CASE("QueueFamilyIndices::isComplete requires graphics and present") {
  // Pure value-type logic: no device needed (and no invalid null-surface
  // query).
  vkutil::QueueFamilyIndices indices;
  CHECK_FALSE(indices.isComplete());

  indices.graphicsFamily = 0;
  CHECK_FALSE(indices.isComplete());
  CHECK(indices.graphicsFamily.has_value());

  indices.presentFamily = 1;
  CHECK(indices.isComplete());
  CHECK(indices.presentFamily.has_value());

  vkutil::QueueFamilyIndices presentOnly;
  presentOnly.presentFamily = 0;
  CHECK_FALSE(presentOnly.isComplete());
}

// -----------------------------------------------------------------------------
// querySwapChainSupport
// -----------------------------------------------------------------------------

TEST_CASE("querySwapChainSupport passes surface queries through unchanged") {
  VulkanTestFixture fx;
  if (!fx) return;
  if (!fx.tryCreateHeadlessSurface()) return;

  // Ask the driver directly, exactly as the helper should, and compare value
  // for value. This asserts the pass-through contract itself, which is what
  // the helper promises — it does not assume the headless surface reports
  // usable capabilities (some ICDs, lavapipe included, report all zeroes for a
  // headless surface because there is no presentation engine behind it).
  VkSurfaceCapabilitiesKHR directCapabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(fx.physicalDevice, fx.surface,
                                            &directCapabilities);

  uint32_t formatCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(fx.physicalDevice, fx.surface,
                                       &formatCount, nullptr);
  std::vector<VkSurfaceFormatKHR> directFormats(formatCount);
  if (formatCount != 0) {
    vkGetPhysicalDeviceSurfaceFormatsKHR(fx.physicalDevice, fx.surface,
                                         &formatCount, directFormats.data());
  }

  uint32_t presentModeCount = 0;
  vkGetPhysicalDeviceSurfacePresentModesKHR(fx.physicalDevice, fx.surface,
                                            &presentModeCount, nullptr);
  std::vector<VkPresentModeKHR> directPresentModes(presentModeCount);
  if (presentModeCount != 0) {
    vkGetPhysicalDeviceSurfacePresentModesKHR(fx.physicalDevice, fx.surface,
                                              &presentModeCount,
                                              directPresentModes.data());
  }

  const vkutil::SwapChainSupportDetails details =
      vkutil::querySwapChainSupport(fx.physicalDevice, fx.surface);

  CHECK(details.capabilities.minImageCount == directCapabilities.minImageCount);
  CHECK(details.capabilities.maxImageCount == directCapabilities.maxImageCount);
  CHECK(details.capabilities.currentExtent.width ==
        directCapabilities.currentExtent.width);
  CHECK(details.capabilities.currentExtent.height ==
        directCapabilities.currentExtent.height);
  CHECK(details.capabilities.minImageExtent.width ==
        directCapabilities.minImageExtent.width);
  CHECK(details.capabilities.minImageExtent.height ==
        directCapabilities.minImageExtent.height);
  CHECK(details.capabilities.maxImageExtent.width ==
        directCapabilities.maxImageExtent.width);
  CHECK(details.capabilities.maxImageExtent.height ==
        directCapabilities.maxImageExtent.height);
  CHECK(details.capabilities.maxImageArrayLayers ==
        directCapabilities.maxImageArrayLayers);
  CHECK(details.capabilities.supportedTransforms ==
        directCapabilities.supportedTransforms);
  CHECK(details.capabilities.currentTransform ==
        directCapabilities.currentTransform);
  CHECK(details.capabilities.supportedCompositeAlpha ==
        directCapabilities.supportedCompositeAlpha);
  CHECK(details.capabilities.supportedUsageFlags ==
        directCapabilities.supportedUsageFlags);

  REQUIRE(details.formats.size() == directFormats.size());
  for (size_t i = 0; i < directFormats.size(); ++i) {
    CHECK(details.formats[i].format == directFormats[i].format);
    CHECK(details.formats[i].colorSpace == directFormats[i].colorSpace);
  }

  REQUIRE(details.presentModes.size() == directPresentModes.size());
  for (size_t i = 0; i < directPresentModes.size(); ++i) {
    CHECK(details.presentModes[i] == directPresentModes[i]);
  }

  // Spec self-consistency that must hold whenever the driver does report
  // capabilities: maxImageCount == 0 means "no limit", otherwise it bounds
  // minImageCount; and FIFO must be supported on any real presentation engine.
  // (A zeroed headless-surface answer fails minImageCount > 0 and is skipped
  // rather than asserted against.)
  if (details.capabilities.minImageCount > 0) {
    CHECK((details.capabilities.maxImageCount == 0 ||
           details.capabilities.maxImageCount >=
               details.capabilities.minImageCount));
  }
  if (!details.presentModes.empty()) {
    bool hasFifo = false;
    for (VkPresentModeKHR mode : details.presentModes) {
      if (mode == VK_PRESENT_MODE_FIFO_KHR) hasFifo = true;
    }
    CHECK(hasFifo);
  }
}

// -----------------------------------------------------------------------------
// createShaderModule (success path)
// -----------------------------------------------------------------------------

TEST_CASE("createShaderModule creates a shader module from valid SPIR-V") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkShaderModule module = vkutil::createShaderModule(
      fx.device, kMinimalFragmentSpirv, sizeof(kMinimalFragmentSpirv));
  REQUIRE(module != VK_NULL_HANDLE);

  // A created module must be destroyable and is not leaked; destroying it is
  // the only portable observation available without building a pipeline.
  vkDestroyShaderModule(fx.device, module, nullptr);
}
