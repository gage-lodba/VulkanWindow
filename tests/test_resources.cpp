#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "VulkanResources.h"
#include "VulkanUtils.h"
#include "test_fixture.h"

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

TEST_CASE("createMappedBuffer can hold floating-point data") {
  VulkanTestFixture fx;
  if (!fx) return;

  constexpr size_t kCount = 16;
  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, kCount * sizeof(float), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

  CHECK(buf.mapped != nullptr);
  auto* floats = static_cast<float*>(buf.mapped);
  for (size_t i = 0; i < kCount; ++i) floats[i] = static_cast<float>(i);
  CHECK(floats[0] == 0.0F);
  CHECK(floats[kCount - 1] == static_cast<float>(kCount - 1));

  vkutil::destroyBuffer(fx.allocator, buf);
}

// -----------------------------------------------------------------------------
// createDeviceLocalBuffer
// -----------------------------------------------------------------------------

TEST_CASE("createDeviceLocalBuffer uploads data into a device-local buffer") {
  VulkanTestFixture fx;
  if (!fx) return;

  const std::array<float, 12> kData = {0.0F, 1.0F, 2.0F, 3.0F, 4.0F,  5.0F,
                                       6.0F, 7.0F, 8.0F, 9.0F, 10.0F, 11.0F};
  const VkDeviceSize kSize = kData.size() * sizeof(float);

  vkutil::Buffer buf = vkutil::createDeviceLocalBuffer(
      fx.allocator, fx.device, fx.graphicsQueue, fx.graphicsQueueFamily,
      kData.data(), kSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

  CHECK(buf.buffer != VK_NULL_HANDLE);
  CHECK(buf.allocation != VK_NULL_HANDLE);
  CHECK(buf.size == kSize);
  // Device-local: no host mapping.
  CHECK(buf.mapped == nullptr);

  vkutil::destroyBuffer(fx.allocator, buf);
}

TEST_CASE("createDeviceLocalBuffer with single byte works") {
  VulkanTestFixture fx;
  if (!fx) return;

  const uint8_t byte = 0x42;
  vkutil::Buffer buf = vkutil::createDeviceLocalBuffer(
      fx.allocator, fx.device, fx.graphicsQueue, fx.graphicsQueueFamily, &byte,
      1, VK_BUFFER_USAGE_INDEX_BUFFER_BIT);

  CHECK(buf.size == 1);
  CHECK(buf.buffer != VK_NULL_HANDLE);

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

  vkutil::destroyImage(fx.allocator, fx.device, tex);
  CHECK(tex.image == VK_NULL_HANDLE);
  CHECK(tex.view == VK_NULL_HANDLE);
}

TEST_CASE("createTexture2D with mipmaps generates a full chain") {
  VulkanTestFixture fx;
  if (!fx) return;

  // 4×4 RGBA8; lavapipe supports R8G8B8A8_UNORM linear blits for mipmaps.
  constexpr uint32_t kW = 4;
  constexpr uint32_t kH = 4;
  std::array<uint8_t, kW * kH * 4> kPixels{};
  for (auto& b : kPixels) b = 128;  // mid-grey

  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, kPixels.data(), kPixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/true);

  CHECK(tex.image != VK_NULL_HANDLE);
  CHECK(tex.view != VK_NULL_HANDLE);
  CHECK(tex.mipLevels > 1);  // 4×4 → 3 levels

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

// -----------------------------------------------------------------------------
// transitionImageLayout
// -----------------------------------------------------------------------------

TEST_CASE(
    "transitionImageLayout records UNDEFINED -> TRANSFER_DST_OPTIMAL barrier") {
  VulkanTestFixture fx;
  if (!fx) return;

  // Create a bare image via VMA (no createTexture2D — we only need an image
  // handle to record a barrier against).
  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {.width = 4, .height = 4, .depth = 1};
  imageInfo.mipLevels = 2;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  vkutil::vkCheck(vmaCreateImage(fx.allocator, &imageInfo, &allocInfo, &image,
                                 &allocation, nullptr),
                  "Failed to create test image");

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // Branch 1: UNDEFINED → TRANSFER_DST_OPTIMAL (2 mips).
  CHECK_NOTHROW(
      vkutil::transitionImageLayout(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 2));
  // Branch 2: TRANSFER_DST → SHADER_READ_ONLY (single mip).
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

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {.width = 4, .height = 4, .depth = 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  vkutil::vkCheck(vmaCreateImage(fx.allocator, &imageInfo, &allocInfo, &image,
                                 &allocation, nullptr),
                  "Failed to create test image");

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
    "transitionImageLayout uses conservative fallback for unrecognised "
    "layout pair") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {.width = 4, .height = 4, .depth = 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

  VmaAllocationCreateInfo allocInfo{};
  allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
  VkImage image = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  vkutil::vkCheck(vmaCreateImage(fx.allocator, &imageInfo, &allocInfo, &image,
                                 &allocation, nullptr),
                  "Failed to create test image");

  VkCommandBuffer cmd =
      vkutil::beginSingleTimeCommands(fx.device, fx.commandPool);

  // Branch 4 (conservative fallback): layout pair not in the known set.
  CHECK_NOTHROW(vkutil::transitionImageLayout(
      cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
      1));

  vkutil::endSingleTimeCommands(fx.device, fx.commandPool, fx.graphicsQueue,
                                cmd);
  vmaDestroyImage(fx.allocator, image, allocation);
}

// -----------------------------------------------------------------------------
// Non-square mipmap generation
// -----------------------------------------------------------------------------

TEST_CASE(
    "createTexture2D generates correct mip levels for a non-square image") {
  VulkanTestFixture fx;
  if (!fx) return;

  // 4×8 RGBA8 — non-square; max(4,8)→4 levels.
  constexpr uint32_t kW = 4;
  constexpr uint32_t kH = 8;
  std::array<uint8_t, kW * kH * 4> kPixels{};
  for (auto& b : kPixels) b = 128;

  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, kPixels.data(), kPixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/true);

  CHECK(tex.mipLevels == 4);  // floor(log2(8)) + 1 = 4

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
