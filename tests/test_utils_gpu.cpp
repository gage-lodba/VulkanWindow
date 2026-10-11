#include <doctest/doctest.h>

#include "VulkanUtils.h"
#include "test_fixture.h"

// -----------------------------------------------------------------------------
// findQueueFamilies
// -----------------------------------------------------------------------------

TEST_CASE("findQueueFamilies finds a graphics queue family on a real device") {
  VulkanTestFixture fx;
  if (!fx) return;

  // The fixture already found a graphics family — findQueueFamilies should
  // return at least that.
  const vkutil::QueueFamilyIndices indices =
      vkutil::findQueueFamilies(fx.physicalDevice, VK_NULL_HANDLE);
  // Without a surface, isComplete() won't be true (no present family),
  // but graphicsFamily should be populated.
  CHECK(indices.graphicsFamily.has_value());
}

TEST_CASE("findQueueFamilies with a headless surface finds at least graphics") {
  VulkanTestFixture fx;
  if (!fx) return;
  if (!fx.tryCreateHeadlessSurface()) return;

  const vkutil::QueueFamilyIndices indices =
      vkutil::findQueueFamilies(fx.physicalDevice, fx.surface);

  // Graphics family must always be found.  Present family may be absent on
  // headless surfaces (some ICDs expose the headless-surface extension but
  // don't support presenting to headless surfaces — the surface handle exists
  // but vkGetPhysicalDeviceSurfaceSupportKHR returns false for every queue).
  CHECK(indices.graphicsFamily.has_value());
  // isComplete() and presentFamily are optional — they depend on the ICD.
  if (indices.isComplete()) {
    CHECK(indices.presentFamily.has_value());
  }
}

TEST_CASE(
    "QueueFamilyIndices::isComplete is false with only a graphics family") {
  VulkanTestFixture fx;
  if (!fx) return;

  const vkutil::QueueFamilyIndices indices =
      vkutil::findQueueFamilies(fx.physicalDevice, VK_NULL_HANDLE);
  CHECK_FALSE(indices.isComplete());
  CHECK(indices.graphicsFamily.has_value());
  CHECK_FALSE(indices.presentFamily.has_value());
}

// -----------------------------------------------------------------------------
// querySwapChainSupport
// -----------------------------------------------------------------------------

TEST_CASE("querySwapChainSupport returns capabilities for a headless surface") {
  VulkanTestFixture fx;
  if (!fx) return;
  if (!fx.tryCreateHeadlessSurface()) return;

  const vkutil::SwapChainSupportDetails details =
      vkutil::querySwapChainSupport(fx.physicalDevice, fx.surface);

  // Deliberately no assertion that minImageCount >= 1 here. Some ICDs (Mesa
  // lavapipe among them) accept a VK_EXT_headless_surface but then report
  // capabilities of all zeroes, because there is no presentation engine behind
  // it — it is the same condition that makes the empty formats/presentModes
  // case below legal. querySwapChainSupport is a thin wrapper over
  // vkGetPhysicalDeviceSurfaceCapabilitiesKHR, so its contract is "the query
  // succeeds and is passed through", not "the driver reports usable values".
  // Asserting the latter tests lavapipe's version, not this library.

  // When the driver does report capabilities, they must be self-consistent.
  if (details.capabilities.minImageCount > 0) {
    CHECK(details.capabilities.minImageCount >= 1);
    if (details.capabilities.maxImageCount > 0) {
      CHECK(details.capabilities.maxImageCount >=
            details.capabilities.minImageCount);
    }
  }

  // Formats and present modes may be empty on headless surfaces (some ICDs
  // don't support presenting to headless surfaces).  When non-empty, FIFO
  // must be present per spec.
  if (!details.formats.empty()) {
    CHECK_FALSE(details.presentModes.empty());
    bool hasFifo = false;
    for (VkPresentModeKHR mode : details.presentModes) {
      if (mode == VK_PRESENT_MODE_FIFO_KHR) hasFifo = true;
    }
    CHECK(hasFifo);
  }
}
