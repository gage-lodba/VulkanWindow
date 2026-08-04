#include <doctest/doctest.h>

#include <array>
#include <cstdint>

#include "VulkanDescriptors.h"
#include "VulkanResources.h"
#include "test_fixture.h"

// -----------------------------------------------------------------------------
// createDescriptorSetLayout
// -----------------------------------------------------------------------------

TEST_CASE("createDescriptorSetLayout creates a layout from one binding") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkDescriptorSetLayout layout = vkutil::createDescriptorSetLayout(
      fx.device,
      {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT}});
  CHECK(layout != VK_NULL_HANDLE);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
}

TEST_CASE("createDescriptorSetLayout with two bindings") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkDescriptorSetLayout layout = vkutil::createDescriptorSetLayout(
      fx.device,
      {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT},
       {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_SHADER_STAGE_FRAGMENT_BIT}});
  CHECK(layout != VK_NULL_HANDLE);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
}

TEST_CASE("createDescriptorSetLayout with empty bindings is valid") {
  VulkanTestFixture fx;
  if (!fx) return;

  // An empty initializer list is a valid (if unusual) layout.
  VkDescriptorSetLayout layout =
      vkutil::createDescriptorSetLayout(fx.device, {});
  CHECK(layout != VK_NULL_HANDLE);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
}

// -----------------------------------------------------------------------------
// createDescriptorPool
// -----------------------------------------------------------------------------

TEST_CASE("createDescriptorPool creates a pool covering one type") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkDescriptorPool pool = vkutil::createDescriptorPool(
      fx.device, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, /*maxSets=*/1);
  CHECK(pool != VK_NULL_HANDLE);
  vkDestroyDescriptorPool(fx.device, pool, nullptr);
}

TEST_CASE("createDescriptorPool with multiple pool sizes") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkDescriptorPool pool = vkutil::createDescriptorPool(
      fx.device,
      {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2},
       {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}},
      /*maxSets=*/4);
  CHECK(pool != VK_NULL_HANDLE);
  vkDestroyDescriptorPool(fx.device, pool, nullptr);
}

// -----------------------------------------------------------------------------
// allocateDescriptorSet
// -----------------------------------------------------------------------------

TEST_CASE("allocateDescriptorSet returns a set for the layout") {
  VulkanTestFixture fx;
  if (!fx) return;

  VkDescriptorSetLayout layout = vkutil::createDescriptorSetLayout(
      fx.device,
      {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT}});
  VkDescriptorPool pool = vkutil::createDescriptorPool(
      fx.device, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, /*maxSets=*/1);

  VkDescriptorSet set = vkutil::allocateDescriptorSet(fx.device, pool, layout);
  CHECK(set != VK_NULL_HANDLE);

  // The set is freed implicitly when the pool is destroyed.
  vkDestroyDescriptorPool(fx.device, pool, nullptr);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
}

// -----------------------------------------------------------------------------
// updateBufferDescriptor
// -----------------------------------------------------------------------------

TEST_CASE("updateBufferDescriptor writes a descriptor for a mapped buffer") {
  VulkanTestFixture fx;
  if (!fx) return;

  // Create a real mapped buffer to point at.
  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, 256, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);

  VkDescriptorSetLayout layout = vkutil::createDescriptorSetLayout(
      fx.device,
      {{0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT}});
  VkDescriptorPool pool = vkutil::createDescriptorPool(
      fx.device, {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}}, /*maxSets=*/1);
  VkDescriptorSet set = vkutil::allocateDescriptorSet(fx.device, pool, layout);

  CHECK_NOTHROW(
      vkutil::updateBufferDescriptor(fx.device, set, 0, buf.buffer, buf.size));

  vkDestroyDescriptorPool(fx.device, pool, nullptr);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
  vkutil::destroyBuffer(fx.allocator, buf);
}

TEST_CASE("updateBufferDescriptor with explicit type and offset") {
  VulkanTestFixture fx;
  if (!fx) return;

  vkutil::Buffer buf = vkutil::createMappedBuffer(
      fx.allocator, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);

  VkDescriptorSetLayout layout = vkutil::createDescriptorSetLayout(
      fx.device,
      {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT}});
  VkDescriptorPool pool = vkutil::createDescriptorPool(
      fx.device, {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1}}, /*maxSets=*/1);
  VkDescriptorSet set = vkutil::allocateDescriptorSet(fx.device, pool, layout);

  // Write the second half of the buffer with an explicit offset.
  const VkDeviceSize kOffset = 512;
  const VkDeviceSize kRange = 512;
  CHECK_NOTHROW(vkutil::updateBufferDescriptor(
      fx.device, set, 0, buf.buffer, kRange, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
      kOffset));

  vkDestroyDescriptorPool(fx.device, pool, nullptr);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
  vkutil::destroyBuffer(fx.allocator, buf);
}

// -----------------------------------------------------------------------------
// updateImageSamplerDescriptor
// -----------------------------------------------------------------------------

TEST_CASE(
    "updateImageSamplerDescriptor writes a combined image sampler descriptor") {
  VulkanTestFixture fx;
  if (!fx) return;

  // Need a real texture and sampler for the descriptor to reference.
  constexpr uint32_t kW = 2;
  constexpr uint32_t kH = 2;
  const std::array<uint8_t, kW * kH * 4> kPixels = {
      255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 128, 128, 128, 255};

  vkutil::Image tex = vkutil::createTexture2D(
      fx.allocator, fx.physicalDevice, fx.device, fx.graphicsQueue,
      fx.graphicsQueueFamily, kPixels.data(), kPixels.size(), kW, kH,
      VK_FORMAT_R8G8B8A8_UNORM, /*generateMipmaps=*/false);
  VkSampler sampler = vkutil::createSampler(fx.device);

  VkDescriptorSetLayout layout = vkutil::createDescriptorSetLayout(
      fx.device, {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   VK_SHADER_STAGE_FRAGMENT_BIT}});
  VkDescriptorPool pool = vkutil::createDescriptorPool(
      fx.device, {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}},
      /*maxSets=*/1);
  VkDescriptorSet set = vkutil::allocateDescriptorSet(fx.device, pool, layout);

  CHECK_NOTHROW(vkutil::updateImageSamplerDescriptor(fx.device, set, 0,
                                                     tex.view, sampler));

  vkDestroyDescriptorPool(fx.device, pool, nullptr);
  vkDestroyDescriptorSetLayout(fx.device, layout, nullptr);
  vkDestroySampler(fx.device, sampler, nullptr);
  vkutil::destroyImage(fx.allocator, fx.device, tex);
}
