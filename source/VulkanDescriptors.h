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
/// Binding fields are passed through as-is: the caller is responsible for
/// satisfying Vulkan valid usage (binding indices, descriptor types, counts and
/// stage flags); invalid values surface as driver/validation errors rather than
/// from this helper.
///
///     auto layout = vkutil::createDescriptorSetLayout(device, {
///         {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
///          VK_SHADER_STAGE_FRAGMENT_BIT},
///     });
[[nodiscard]] auto createDescriptorSetLayout(
    VkDevice device,
    std::initializer_list<DescriptorBinding> bindings) -> VkDescriptorSetLayout;

/// Create a descriptor pool sized for `maxSets` sets covering the given pool
/// sizes (created with FREE_DESCRIPTOR_SET_BIT). Throws std::invalid_argument
/// when `maxSets` is 0 (Vulkan requires a positive value); throws
/// std::runtime_error if vkCreateDescriptorPool fails. Destroy with
/// vkDestroyDescriptorPool.
///
/// Sizing is the caller's job: `poolSizes` must contain, for every descriptor
/// type used by the layouts allocated from this pool, at least the total
/// descriptor count those sets need (e.g. count_per_set * maxSets). The helper
/// does not derive it from the layouts — under-sized pools fail later in
/// allocateDescriptorSet() with VK_ERROR_OUT_OF_POOL_MEMORY.
[[nodiscard]] auto createDescriptorPool(
    VkDevice device, std::initializer_list<VkDescriptorPoolSize> poolSizes,
    uint32_t maxSets) -> VkDescriptorPool;

/// Allocate a single descriptor set with `layout` from `pool`. Throws on
/// failure.
[[nodiscard]] auto allocateDescriptorSet(VkDevice device, VkDescriptorPool pool,
                                         VkDescriptorSetLayout layout)
    -> VkDescriptorSet;

/// Point `binding` of `set` at a sampled texture (combined image sampler);
/// `view` is assumed to be in SHADER_READ_ONLY_OPTIMAL (as createTexture2D
/// leaves it). Set/layout compatibility and the binding's descriptor type are
/// not checked; mismatches surface through the validation layer.
void updateImageSamplerDescriptor(VkDevice device, VkDescriptorSet set,
                                  uint32_t binding, VkImageView view,
                                  VkSampler sampler);

/// Point `binding` of `set` at a buffer range (uniform by default).
///
/// Caller responsibilities (not validated — device limits are not known here):
/// for VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, `range` must not exceed
/// maxUniformBufferRange and `offset` must be a multiple of
/// minUniformBufferOffsetAlignment; for VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
/// `range` must not exceed maxStorageBufferRange and `offset` must be a
/// multiple of minStorageBufferOffsetAlignment.
void updateBufferDescriptor(
    VkDevice device, VkDescriptorSet set, uint32_t binding, VkBuffer buffer,
    VkDeviceSize range,
    VkDescriptorType type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
    VkDeviceSize offset = 0);

}  // namespace vkutil
