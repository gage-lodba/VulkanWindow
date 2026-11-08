#include <doctest/doctest.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>
#include <vector>

#include "VulkanResources.h"
#include "VulkanUtils.h"
#include "test_fixture.h"

// -----------------------------------------------------------------------------
// Readback helpers
// -----------------------------------------------------------------------------
//
// These copy GPU-side results back to host memory through a one-shot staging
// buffer, so the tests below can assert on the bytes the device actually holds
// instead of only on handle creation. They only use helpers under test
// (beginSingleTimeCommands / endSingleTimeCommands / createMappedBuffer /
// destroyBuffer), so a regression in those shows up here too.

namespace {

constexpr uint32_t kRgba8BytesPerPixel = 4;

/// Copy `size` bytes out of a device buffer into host memory.
auto readBackBuffer(VulkanTestFixture& fx, VkBuffer source,
                    VkDeviceSize size) -> std::vector<uint8_t> {
  vkutil::Buffer staging = vkutil::createMappedBuffer(
      fx.allocator, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  REQUIRE(staging.mapped != nullptr);

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);
  VkBufferCopy copyRegion{};
  copyRegion.size = size;
  vkCmdCopyBuffer(cmd, source, staging.buffer, 1, &copyRegion);

  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                       0, nullptr);
  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);

  // The staging allocation need not be host-coherent; make the device writes
  // visible to the CPU before reading them.
  vkutil::vkCheck(vmaInvalidateAllocation(fx.allocator, staging.allocation, 0,
                                          VK_WHOLE_SIZE),
                  "Failed to invalidate readback staging buffer");

  std::vector<uint8_t> bytes(static_cast<size_t>(size));
  std::memcpy(bytes.data(), staging.mapped, bytes.size());
  vkutil::destroyBuffer(fx.allocator, staging);
  return bytes;
}

/// Copy every mip level of an RGBA8 image out of the device. Assumes the image
/// is in SHADER_READ_ONLY_OPTIMAL and has TRANSFER_SRC usage, which is exactly
/// what createTexture2D(…, generateMipmaps = true) leaves behind.
auto readBackMipLevels(VulkanTestFixture& fx, VkImage image, uint32_t mipLevels,
                       uint32_t width,
                       uint32_t height) -> std::vector<std::vector<uint8_t>> {
  std::vector<VkBufferImageCopy> regions;
  std::vector<VkDeviceSize> offsets;
  std::vector<size_t> sizes;
  VkDeviceSize total = 0;
  for (uint32_t level = 0; level < mipLevels; ++level) {
    const uint32_t levelWidth = std::max(1U, width >> level);
    const uint32_t levelHeight = std::max(1U, height >> level);
    const auto levelSize = static_cast<VkDeviceSize>(levelWidth) * levelHeight *
                           kRgba8BytesPerPixel;
    offsets.push_back(total);
    sizes.push_back(static_cast<size_t>(levelSize));

    VkBufferImageCopy region{};
    region.bufferOffset = total;  // RGBA8 level sizes are all 4-byte multiples
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = level;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {
        .width = levelWidth, .height = levelHeight, .depth = 1};
    regions.push_back(region);
    total += levelSize;
  }

  vkutil::Buffer staging = vkutil::createMappedBuffer(
      fx.allocator, total, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  REQUIRE(staging.mapped != nullptr);

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);
  // All levels are SHADER_READ_ONLY_OPTIMAL after createTexture2D; a copy
  // source must be in TRANSFER_SRC_OPTIMAL (or GENERAL). The fallback branch of
  // transitionImageLayout records this barrier because the pair is not one of
  // the specialised transitions.
  vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, mipLevels);
  vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         staging.buffer, static_cast<uint32_t>(regions.size()),
                         regions.data());

  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                       0, nullptr);
  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);

  vkutil::vkCheck(vmaInvalidateAllocation(fx.allocator, staging.allocation, 0,
                                          VK_WHOLE_SIZE),
                  "Failed to invalidate readback staging buffer");

  std::vector<std::vector<uint8_t>> levels;
  levels.reserve(mipLevels);
  const auto* base = static_cast<const uint8_t*>(staging.mapped);
  for (uint32_t level = 0; level < mipLevels; ++level) {
    std::vector<uint8_t> bytes(sizes[level]);
    std::memcpy(bytes.data(), base + offsets[level], bytes.size());
    levels.push_back(std::move(bytes));
  }
  vkutil::destroyBuffer(fx.allocator, staging);
  return levels;
}

auto allBytesNear(const std::vector<uint8_t>& bytes, uint8_t expected,
                  int tolerance) -> bool {
  return std::all_of(bytes.begin(), bytes.end(), [&](uint8_t value) {
    return std::abs(static_cast<int>(value) - static_cast<int>(expected)) <=
           tolerance;
  });
}

/// Create a bare VMA-backed 2D image (no view) for layout-transition tests.
/// Throws via vkCheck on failure.
auto makeTestImage(VulkanTestFixture& fx, VkFormat format,
                   VkImageUsageFlags usage, uint32_t width, uint32_t height,
                   uint32_t mipLevels) -> std::pair<VkImage, VmaAllocation> {
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {.width = width, .height = height, .depth = 1};
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  vkutil::vkCheck(vmaCreateImage(fx.allocator, &imageInfo, &allocInfo, &image,
                                 &allocation, nullptr),
                  "Failed to create test image");
  return {image, allocation};
}

}  // namespace

// -----------------------------------------------------------------------------
// Argument guards on the create* helpers
// -----------------------------------------------------------------------------
//
// Every guard here is documented to throw std::invalid_argument and to run
// before the helper touches VMA or Vulkan. That ordering is what makes these
// cases runnable on any machine — no ICD, device or allocator is needed — and
// it is also the contract: a caller must get an actionable exception instead of
// an undefined-behaviour Vulkan call with a zero-sized buffer/image. If a guard
// ever moves after the first Vulkan call, these tests start crashing on the
// null handles instead of silently passing, which is the intended signal.

TEST_CASE("createMappedBuffer rejects a zero size") {
  CHECK_THROWS_AS((void)vkutil::createMappedBuffer(
                      VK_NULL_HANDLE, 0, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),
                  std::invalid_argument);
}

TEST_CASE("createDeviceLocalBuffer rejects zero size and null data") {
  constexpr uint8_t kByte = 0x42;
  // VkBufferCreateInfo.size must be > 0 (VUID-VkBufferCreateInfo-size-00912).
  CHECK_THROWS_AS((void)vkutil::createDeviceLocalBuffer(
                      VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0, &kByte,
                      0, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                  std::invalid_argument);
  // A positive size with no source bytes would make the helper memcpy from
  // null.
  CHECK_THROWS_AS((void)vkutil::createDeviceLocalBuffer(
                      VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, 0,
                      nullptr, 4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
                  std::invalid_argument);
}

TEST_CASE("createTexture2D rejects empty dimensions, data and dataSize") {
  const std::array<uint8_t, 4> kPixel = {0, 0, 0, 255};
  // VkImageCreateInfo.extent requires every dimension to be > 0
  // (VUID-VkImageCreateInfo-extent-00944/00945/00946).
  CHECK_THROWS_AS((void)vkutil::createTexture2D(
                      VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                      VK_NULL_HANDLE, 0, kPixel.data(), kPixel.size(), 0, 1),
                  std::invalid_argument);
  CHECK_THROWS_AS((void)vkutil::createTexture2D(
                      VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                      VK_NULL_HANDLE, 0, kPixel.data(), kPixel.size(), 1, 0),
                  std::invalid_argument);
  // An empty upload has no level-0 texels to copy.
  CHECK_THROWS_AS((void)vkutil::createTexture2D(VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                VK_NULL_HANDLE, VK_NULL_HANDLE,
                                                0, kPixel.data(), 0, 1, 1),
                  std::invalid_argument);
  // A positive upload size with no source pixels would make the helper memcpy
  // from null.
  CHECK_THROWS_AS((void)vkutil::createTexture2D(
                      VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE,
                      VK_NULL_HANDLE, 0, nullptr, kPixel.size(), 1, 1),
                  std::invalid_argument);
}

// -----------------------------------------------------------------------------
// createMappedBuffer / destroyBuffer
// -----------------------------------------------------------------------------

TEST_CASE("createMappedBuffer returns a mapped host-visible buffer") {
  VulkanTestFixture fx;
  if (!fx) return;

  constexpr VkDeviceSize kSize = 256;
  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, kSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

  CHECK(buf.buffer != VK_NULL_HANDLE);
  CHECK(buf.allocation != VK_NULL_HANDLE);
  CHECK(buf.size == kSize);
  CHECK(buf.mapped != nullptr);

  // Write through the mapped pointer and verify host reads see the write.
  std::memset(buf.mapped, 0xAB, static_cast<size_t>(kSize));
  const auto* bytes = static_cast<const uint8_t*>(buf.mapped);
  CHECK(bytes[0] == 0xAB);
  CHECK(bytes[kSize - 1] == 0xAB);

  vkutil::destroyBuffer(fx.allocator, buf);

  CHECK(buf.buffer == VK_NULL_HANDLE);
  CHECK(buf.allocation == VK_NULL_HANDLE);
  CHECK(buf.size == 0);
  CHECK(buf.mapped == nullptr);
}

// -----------------------------------------------------------------------------
// flushMapped
// -----------------------------------------------------------------------------

TEST_CASE(
    "flushMapped succeeds on a mapped buffer and preserves its contents") {
  VulkanTestFixture fx;
  if (!fx) return;

  constexpr VkDeviceSize kSize = 128;
  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, kSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  REQUIRE(buf.mapped != nullptr);

  std::memset(buf.mapped, 0x5A, static_cast<size_t>(kSize));

  // Must not throw: vmaFlushAllocation only calls vkFlushMappedMemoryRanges for
  // non-coherent allocations, so on the coherent memory most drivers hand out
  // this is a no-op, and on the rest it has to make the write visible.
  CHECK_NOTHROW(vkutil::flushMapped(fx.allocator, buf));

  // Flushing publishes the CPU writes; it must not alter them.
  const auto* bytes = static_cast<const uint8_t*>(buf.mapped);
  CHECK(bytes[0] == 0x5A);
  CHECK(bytes[kSize - 1] == 0x5A);

  vkutil::destroyBuffer(fx.allocator, buf);
}

TEST_CASE("flushMapped is a no-op on an empty or destroyed buffer") {
  VulkanTestFixture fx;
  if (!fx) return;

  // A default-constructed Buffer has no allocation; flushMapped returns before
  // touching VMA, so it can't be handed a null allocation.
  vkutil::Buffer empty;
  CHECK_NOTHROW(vkutil::flushMapped(fx.allocator, empty));

  // Same after destroyBuffer() has nulled the handles.
  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  vkutil::destroyBuffer(fx.allocator, buf);
  CHECK_NOTHROW(vkutil::flushMapped(fx.allocator, buf));
}

// -----------------------------------------------------------------------------
// createDeviceLocalBuffer
// -----------------------------------------------------------------------------
//
// Coverage limit for the exception-safety fix in this helper (staging buffer
// destroyed if the staging flush throws): provoking vmaFlushAllocation failure
// would need fault injection into VMA/ the driver. A mock allocator would test
// the mock, not the helper, and the real failure is not reachable through the
// public API on a healthy driver — so this path is reviewed by inspection in
// source/VulkanResources.cpp (staging is owned outside the try block and is
// destroyed by the catch handler) instead of asserted here. The happy path and
// the argument guards are covered below/above.

TEST_CASE("createDeviceLocalBuffer uploads the exact bytes to device memory") {
  VulkanTestFixture fx;
  if (!fx) return;

  const std::array<float, 12> kData = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F,  5.0F,
                                       6.0F, 7.0F, 8.0F, 9.0F, 10.0F, 11.0F};
  const VkDeviceSize kSize = kData.size() * sizeof(float);

  // TRANSFER_SRC is needed only for the test's readback copy; the helper adds
  // TRANSFER_DST for its own staging upload.
  vkutil::Buffer buf = vkutil::createDeviceLocalBuffer(
      fx.allocator, fx.device, fx.graphicsQueue, fx.graphicsQueueFamily,
      kData.data(), kSize,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

  CHECK(buf.buffer != VK_NULL_HANDLE);
  CHECK(buf.allocation != VK_NULL_HANDLE);
  CHECK(buf.size == kSize);
  // Device-local: no host mapping.
  CHECK(buf.mapped == nullptr);

  // Read the bytes back through a staging buffer: a helper that skipped or
  // corrupted the copy would pass the handle checks above but fail here.
  const std::vector<uint8_t> readBack = readBackBuffer(fx, buf.buffer, kSize);
  REQUIRE(readBack.size() == static_cast<size_t>(kSize));
  CHECK(std::memcmp(readBack.data(), kData.data(), readBack.size()) == 0);

  vkutil::destroyBuffer(fx.allocator, buf);
}

TEST_CASE("createDeviceLocalBuffer with a single byte round-trips") {
  VulkanTestFixture fx;
  if (!fx) return;

  const uint8_t byte = 0x42;
  vkutil::Buffer buf = vkutil::createDeviceLocalBuffer(
      fx.allocator, fx.device, fx.graphicsQueue, fx.graphicsQueueFamily, &byte,
      1, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

  CHECK(buf.size == 1);
  CHECK(buf.buffer != VK_NULL_HANDLE);

  const std::vector<uint8_t> readBack = readBackBuffer(fx, buf.buffer, 1);
  REQUIRE(readBack.size() == 1);
  CHECK(readBack[0] == byte);

  vkutil::destroyBuffer(fx.allocator, buf);
}

// -----------------------------------------------------------------------------
// destroyBuffer no-op paths
// -----------------------------------------------------------------------------

TEST_CASE("destroyBuffer is a no-op on a default-constructed Buffer") {
  VulkanTestFixture fx;
  if (!fx) return;

  vkutil::Buffer buf;
  CHECK_NOTHROW(vkutil::destroyBuffer(fx.allocator, buf));
  CHECK(buf.buffer == VK_NULL_HANDLE);
  CHECK(buf.mapped == nullptr);
}

TEST_CASE("destroyBuffer is a no-op when called twice") {
  VulkanTestFixture fx;
  if (!fx) return;

  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, 64, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
  CHECK(buf.buffer != VK_NULL_HANDLE);

  vkutil::destroyBuffer(fx.allocator, buf);
  CHECK(buf.buffer == VK_NULL_HANDLE);

  // Second destroy must not crash.
  CHECK_NOTHROW(vkutil::destroyBuffer(fx.allocator, buf));
}

// -----------------------------------------------------------------------------
// createSampler
// -----------------------------------------------------------------------------

TEST_CASE("createSampler returns a valid sampler with default parameters") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkSampler sampler = vkutil::createSampler(fx.device);
  CHECK(sampler != VK_NULL_HANDLE);
  vkDestroySampler(fx.device, sampler, nullptr);
}

TEST_CASE("createSampler with nearest filtering and clamp addressing") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkSampler sampler = vkutil::createSampler(
      fx.device, VK_FILTER_NEAREST, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
  CHECK(sampler != VK_NULL_HANDLE);
  vkDestroySampler(fx.device, sampler, nullptr);
}

// -----------------------------------------------------------------------------
// beginSingleTimeCommands / endSingleTimeCommands
// -----------------------------------------------------------------------------
//
// Coverage limit for the begin-failure exception-safety fix (the freshly
// allocated command buffer is freed before the error is rethrown): forcing
// vkBeginCommandBuffer to fail through the public API would require invalid
// usage, and a fault-injection loader would only test the injection harness.
// The happy path is covered here; the failure ordering is reviewed by
// inspection in source/VulkanResources.cpp. The helper's argument-free contract
// (no null-guard on device/pool) is unchanged.

TEST_CASE("beginSingleTimeCommands and endSingleTimeCommands round-trip") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);
  CHECK(cmd != VK_NULL_HANDLE);

  // Record a trivial no-op barrier so the command buffer is non-empty.
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = 0;
  barrier.dstAccessMask = 0;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 1, &barrier, 0,
                       nullptr, 0, nullptr);

  CHECK_NOTHROW(vkutil::endSingleTimeCommands(fx.device, fx.commandPool,
                                              fx.graphicsQueue, cmd));
}

// -----------------------------------------------------------------------------
// createTexture2D / destroyImage
// -----------------------------------------------------------------------------

TEST_CASE("createTexture2D creates a sampled image with a view (no mipmaps)") {
  VulkanTestFixture fx;
  if (!fx) return;

  // A 2×2 solid-red RGBA8 texture.
  constexpr uint32_t kW = 2;
  constexpr uint32_t kH = 2;
  const std::array<uint8_t, kW * kH * 4> kPixels = {
      255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};

  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, kPixels.data(), kPixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/false);

  CHECK(tex.image != VK_NULL_HANDLE);
  CHECK(tex.allocation != VK_NULL_HANDLE);
  CHECK(tex.view != VK_NULL_HANDLE);
  CHECK(tex.extent.width == kW);
  CHECK(tex.extent.height == kH);
  CHECK(tex.format == VK_FORMAT_R8G8B8A8_UNORM);
  CHECK(tex.mipLevels == 1);

  // Coverage limit, stated explicitly: a non-mipmapped texture is created
  // without TRANSFER_SRC usage, so its contents cannot be copied back here.
  // The upload/readback path is covered by the mipmapped cases below (same
  // staging upload code).
  vkutil::destroyImage(fx.allocator, fx.device, tex);
  CHECK(tex.image == VK_NULL_HANDLE);
  CHECK(tex.view == VK_NULL_HANDLE);
}

TEST_CASE("createTexture2D uploads level-0 texels exactly") {
  VulkanTestFixture fx;
  if (!fx) return;

  // 4×4 with a distinct value per texel so a wrong-row/stride upload is caught.
  constexpr uint32_t kW = 4;
  constexpr uint32_t kH = 4;
  std::array<uint8_t, kW * kH * 4> pixels{};
  for (uint32_t y = 0; y < kH; ++y) {
    for (uint32_t x = 0; x < kW; ++x) {
      const size_t index = (static_cast<size_t>(y) * kW + x) * 4;
      pixels[index + 0] = static_cast<uint8_t>(x * 60 + 10);
      pixels[index + 1] = static_cast<uint8_t>(y * 60 + 20);
      pixels[index + 2] = static_cast<uint8_t>(x * 16 + y * 4 + 30);
      pixels[index + 3] = 255;
    }
  }

  // Mipmaps are generated because that is what gives the image TRANSFER_SRC
  // usage; the readback below only inspects level 0.
  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, pixels.data(), pixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/true);

  const auto levels = readBackMipLevels(fx, tex.image, tex.mipLevels, kW, kH);
  REQUIRE(levels.size() == tex.mipLevels);
  REQUIRE(levels[0].size() == pixels.size());
  CHECK(std::memcmp(levels[0].data(), pixels.data(), pixels.size()) == 0);

  vkutil::destroyImage(fx.allocator, fx.device, tex);
}

TEST_CASE("createTexture2D with mipmaps generates the full chain contents") {
  VulkanTestFixture fx;
  if (!fx) return;

  // 4×4 RGBA8 mid-grey: a constant colour is the one case where the exact
  // expected mip content is well-defined (linear filtering of a constant stays
  // constant), so a missing recordMipChain() call shows up as garbage levels
  // instead of a fuzzy tolerance mismatch.
  constexpr uint32_t kW = 4;
  constexpr uint32_t kH = 4;
  constexpr uint8_t kGrey = 128;
  std::array<uint8_t, kW * kH * 4> pixels{};
  for (auto& byte : pixels) byte = kGrey;

  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, pixels.data(), pixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/true);

  // floor(log2(4)) + 1 = 3 levels.
  CHECK(tex.mipLevels == 3);
  CHECK(tex.view != VK_NULL_HANDLE);

  const auto levels = readBackMipLevels(fx, tex.image, tex.mipLevels, kW, kH);
  REQUIRE(levels.size() == 3);
  CHECK(levels[0].size() == 4U * 4U * 4U);
  CHECK(levels[1].size() == 2U * 2U * 4U);
  CHECK(levels[2].size() == 1U * 1U * 4U);
  for (size_t level = 0; level < levels.size(); ++level) {
    INFO("mip level ", level);
    CHECK(allBytesNear(levels[level], kGrey, 1));
  }

  vkutil::destroyImage(fx.allocator, fx.device, tex);
  CHECK(tex.image == VK_NULL_HANDLE);
}

TEST_CASE("createTexture2D with a single-pixel texture") {
  VulkanTestFixture fx;
  if (!fx) return;

  const std::array<uint8_t, 4> kPixel = {0, 255, 0, 255};
  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, kPixel.data(), kPixel.size(), 1, 1,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/false);

  CHECK(tex.extent.width == 1);
  CHECK(tex.extent.height == 1);
  CHECK(tex.mipLevels == 1);

  vkutil::destroyImage(fx.allocator, fx.device, tex);
}

TEST_CASE(
    "createTexture2D throws for a format with no linear blit for mipmaps") {
  VulkanTestFixture fx;
  if (!fx) return;

  // Integer formats cannot be filtered linearly, so the documented mipmap
  // precondition fails and the helper must throw before creating anything.
  constexpr uint32_t kW = 4;
  constexpr uint32_t kH = 4;
  std::array<uint32_t, kW * kH> pixels{};
  CHECK_THROWS_AS(
      (void)vkutil::createTexture2D(
          fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
          fx.graphicsQueueFamily, pixels.data(),
          pixels.size() * sizeof(uint32_t), kW, kH, VK_FORMAT_R32G32B32_SINT,
          /*generateMipmaps=*/true),
      std::runtime_error);
}

// -----------------------------------------------------------------------------
// transitionImageLayout
// -----------------------------------------------------------------------------

TEST_CASE(
    "transitionImageLayout records UNDEFINED -> TRANSFER_DST_OPTIMAL barrier") {
  VulkanTestFixture fx;
  if (!fx) return;

  auto [image, allocation] = makeTestImage(
      fx, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 4, 4, 2);

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // Branch 1: UNDEFINED → TRANSFER_DST_OPTIMAL (2 mips).
  CHECK_NOTHROW(
      vkutil::transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2));
  // Branch 2: TRANSFER_DST → SHADER_READ_ONLY (single mip). The image really is
  // in TRANSFER_DST now because branch 1 was recorded first in this command
  // buffer; that is what makes this barrier valid usage.
  CHECK_NOTHROW(vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1));

  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);
  vmaDestroyImage(fx.allocator, image, allocation);
}

TEST_CASE(
    "transitionImageLayout records UNDEFINED -> SHADER_READ_ONLY_OPTIMAL "
    "barrier") {
  VulkanTestFixture fx;
  if (!fx) return;

  auto [image, allocation] = makeTestImage(
      fx, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, 4, 4, 1);

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // Branch 3: UNDEFINED → SHADER_READ_ONLY_OPTIMAL.
  CHECK_NOTHROW(vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1));

  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);
  vmaDestroyImage(fx.allocator, image, allocation);
}

TEST_CASE(
    "transitionImageLayout uses the conservative fallback for an unknown "
    "layout pair") {
  VulkanTestFixture fx;
  if (!fx) return;

  auto [image, allocation] = makeTestImage(fx, VK_FORMAT_R8G8B8A8_UNORM,
                                           VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                               VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                               VK_IMAGE_USAGE_SAMPLED_BIT,
                                           4, 4, 1);

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // The point of the fallback test is the *helper's* generic branch, so put the
  // image into the real oldLayout with a hand-written, spec-exact barrier
  // first: an UNDEFINED -> TRANSFER_SRC transition with srcAccessMask == 0.
  // (The previous version of this test declared TRANSFER_SRC_OPTIMAL as the
  // old layout of an image that was still UNDEFINED, which is invalid usage —
  // VUID-VkImageMemoryBarrier-oldLayout-01197.)
  VkImageMemoryBarrier initial{};
  initial.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  initial.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  initial.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  initial.srcAccessMask = 0;
  initial.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  initial.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  initial.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  initial.image = image;
  initial.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  initial.subresourceRange.levelCount = 1;
  initial.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &initial);

  // Branch 4 (conservative fallback): layout pair not in the known set. The
  // image is genuinely in TRANSFER_SRC_OPTIMAL now, so the barrier is valid.
  CHECK_NOTHROW(vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
      1));
  // The fallback left it in GENERAL; exercise the fallback again from the real
  // current layout to be sure it is not a one-off.
  CHECK_NOTHROW(vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_GENERAL,
      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1));

  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);
  vmaDestroyImage(fx.allocator, image, allocation);
}

TEST_CASE("transitionImageLayout rejects an invalid subresource range") {
  VulkanTestFixture fx;
  if (!fx) return;

  auto [image, allocation] = makeTestImage(
      fx, VK_FORMAT_R8G8B8A8_UNORM,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, 4, 4, 1);
  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // Both of these would record an invalid VkImageSubresourceRange
  // (levelCount == 0 / aspectMask == 0), so the helper rejects them before any
  // barrier is recorded.
  CHECK_THROWS_AS(vkutil::transitionImageLayout(
                      cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, /*mipLevels=*/0),
                  std::invalid_argument);
  CHECK_THROWS_AS(vkutil::transitionImageLayout(
                      cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, /*mipLevels=*/1,
                      /*aspectMask=*/0),
                  std::invalid_argument);

  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);
  vmaDestroyImage(fx.allocator, image, allocation);
}

TEST_CASE("transitionImageLayout honours a depth aspectMask") {
  VulkanTestFixture fx;
  if (!fx) return;

  // Depth/stencil images take a different aspect and layout pair than colour
  // textures — the aspectMask parameter exists for exactly this case.
  VkFormatProperties formatProperties{};
  vkGetPhysicalDeviceFormatProperties(fx.physicalDevice, VK_FORMAT_D32_SFLOAT,
                                      &formatProperties);
  constexpr VkFormatFeatureFlags kNeeded =
      VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
      VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if ((formatProperties.optimalTilingFeatures & kNeeded) != kNeeded) {
    return;  // driver cannot render to and sample depth; nothing to test
  }

  auto [image, allocation] = makeTestImage(
      fx, VK_FORMAT_D32_SFLOAT,
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
      4, 4, 1);

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // Real current layout first (UNDEFINED), then use the helper for a pair it
  // does not specialise, with VK_IMAGE_ASPECT_DEPTH_BIT.
  VkImageMemoryBarrier initial{};
  initial.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  initial.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  initial.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  initial.srcAccessMask = 0;
  initial.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  initial.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  initial.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  initial.image = image;
  initial.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  initial.subresourceRange.levelCount = 1;
  initial.subresourceRange.layerCount = 1;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0,
                       nullptr, 0, nullptr, 1, &initial);

  CHECK_NOTHROW(vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, 1,
      VK_IMAGE_ASPECT_DEPTH_BIT));

  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);
  vmaDestroyImage(fx.allocator, image, allocation);
}

// -----------------------------------------------------------------------------
// Non-square mipmap generation
// -----------------------------------------------------------------------------

TEST_CASE(
    "createTexture2D generates the correct mip chain for a non-square image") {
  VulkanTestFixture fx;
  if (!fx) return;

  // 4×8 RGBA8 — non-square. The chain follows max(width, height): the smaller
  // dimension clamps at 1, so floor(log2(8)) + 1 = 4 levels: 4×8, 2×4, 1×2,
  // 1×1.
  constexpr uint32_t kW = 4;
  constexpr uint32_t kH = 8;
  constexpr uint8_t kGrey = 128;
  std::array<uint8_t, kW * kH * 4> pixels{};
  for (auto& byte : pixels) byte = kGrey;

  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, pixels.data(), pixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/true);

  CHECK(tex.mipLevels == 4);
  const auto levels = readBackMipLevels(fx, tex.image, tex.mipLevels, kW, kH);
  REQUIRE(levels.size() == 4);
  CHECK(levels[0].size() == 4U * 8U * 4U);
  CHECK(levels[1].size() == 2U * 4U * 4U);
  CHECK(levels[2].size() == 1U * 2U * 4U);
  CHECK(levels[3].size() == 1U * 1U * 4U);
  for (size_t level = 0; level < levels.size(); ++level) {
    INFO("mip level ", level);
    CHECK(allBytesNear(levels[level], kGrey, 1));
  }

  vkutil::destroyImage(fx.allocator, fx.device, tex);
}

// -----------------------------------------------------------------------------
// destroyImage no-op paths
// -----------------------------------------------------------------------------

TEST_CASE("destroyImage is a no-op on a default-constructed Image") {
  VulkanTestFixture fx;
  if (!fx) return;

  vkutil::Image img;
  CHECK_NOTHROW(vkutil::destroyImage(fx.allocator, fx.device, img));
  CHECK(img.image == VK_NULL_HANDLE);
  CHECK(img.view == VK_NULL_HANDLE);
}

TEST_CASE("destroyImage is a no-op when called twice") {
  VulkanTestFixture fx;
  if (!fx) return;

  // Use the smallest possible texture.
  const std::array<uint8_t, 4> kPixel = {0, 0, 255, 255};
  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, kPixel.data(), kPixel.size(), 1, 1,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/false);

  CHECK(tex.image != VK_NULL_HANDLE);
  vkutil::destroyImage(fx.allocator, fx.device, tex);
  CHECK(tex.image == VK_NULL_HANDLE);

  // Second destroy must not crash.
  CHECK_NOTHROW(vkutil::destroyImage(fx.allocator, fx.device, tex));
}
