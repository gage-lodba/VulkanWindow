#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <initializer_list>

namespace vkutil {

/// One entry in a descriptor set layout.
struct DescriptorBinding {
  uint32_t binding{0};
  VkDescriptorType type{};
  VkShaderStageFlags stageFlags{};
  uint32_t count{1};
};

/// Create a descriptor set layout from a list of bindings. Throws on failure;
/// destroy with vkDestroyDescriptorSetLayout.
///
///     auto layout = vkutil::createDescriptorSetLayout(device, {
///         {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
///          VK_SHADER_STAGE_FRAGMENT_BIT},
///     });
[[nodiscard]] auto createDescriptorSetLayout(
    VkDevice device, std::initializer_list<DescriptorBinding> bindings)
    -> VkDescriptorSetLayout;

/// Create a descriptor pool sized for `maxSets` sets covering the given pool
/// sizes (created with FREE_DESCRIPTOR_SET_BIT). Throws on failure; destroy
/// with vkDestroyDescriptorPool.
[[nodiscard]] auto createDescriptorPool(
    VkDevice device, std::initializer_list<VkDescriptorPoolSize> poolSizes,
    uint32_t maxSets) -> VkDescriptorPool;

/// Allocate a single descriptor set with `layout` from `pool`. Throws on
/// failure.
[[nodiscard]] auto allocateDescriptorSet(VkDevice device,
                                         VkDescriptorPool pool,
                                         VkDescriptorSetLayout layout)
    -> VkDescriptorSet;

/// Point `binding` of `set` at a sampled texture (combined image sampler);
/// `view` is assumed to be in SHADER_READ_ONLY_OPTIMAL (as createTexture2D
/// leaves it).
void updateImageSamplerDescriptor(VkDevice device, VkDescriptorSet set,
                                  uint32_t binding, VkImageView view,
                                  VkSampler sampler);

/// Point `binding` of `set` at a buffer range (uniform by default).
void updateBufferDescriptor(
    VkDevice device, VkDescriptorSet set, uint32_t binding, VkBuffer buffer,
    VkDeviceSize range,
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VkDeviceSize offset = 0);

}  // namespace vkutil
