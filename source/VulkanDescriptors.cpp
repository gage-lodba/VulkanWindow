#include "VulkanDescriptors.h"

#include <vector>

#include "VulkanUtils.h"  // vkCheck

namespace vkutil {

auto createDescriptorSetLayout(VkDevice device,
                               std::initializer_list<DescriptorBinding>
                                   bindings) -> VkDescriptorSetLayout {
  std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
  layoutBindings.reserve(bindings.size());
  for (const DescriptorBinding &binding : bindings) {
    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding.binding;
    layoutBinding.descriptorType = binding.type;
    layoutBinding.descriptorCount = binding.count;
    layoutBinding.stageFlags = binding.stageFlags;
    layoutBindings.push_back(layoutBinding);
  }

  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(layoutBindings.size());
  layoutInfo.pBindings = layoutBindings.data();

  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  vkCheck(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout),
          "Failed to create descriptor set layout");
  return layout;
}

auto createDescriptorPool(VkDevice device,
                          std::initializer_list<VkDescriptorPoolSize> poolSizes,
                          uint32_t maxSets) -> VkDescriptorPool {
  std::vector<VkDescriptorPoolSize> sizes(poolSizes);

  VkDescriptorPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  poolInfo.maxSets = maxSets;
  poolInfo.poolSizeCount = static_cast<uint32_t>(sizes.size());
  poolInfo.pPoolSizes = sizes.data();

  VkDescriptorPool pool = VK_NULL_HANDLE;
  vkCheck(vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool),
          "Failed to create descriptor pool");
  return pool;
}

auto allocateDescriptorSet(VkDevice device, VkDescriptorPool pool,
                           VkDescriptorSetLayout layout) -> VkDescriptorSet {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = pool;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  VkDescriptorSet set = VK_NULL_HANDLE;
  vkCheck(vkAllocateDescriptorSets(device, &allocInfo, &set),
          "Failed to allocate descriptor set");
  return set;
}

void updateImageSamplerDescriptor(VkDevice device, VkDescriptorSet set,
                                  uint32_t binding, VkImageView view,
                                  VkSampler sampler) {
  VkDescriptorImageInfo imageInfo{};
  imageInfo.sampler = sampler;
  imageInfo.imageView = view;
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.descriptorCount = 1;
  write.pImageInfo = &imageInfo;

  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

void updateBufferDescriptor(VkDevice device, VkDescriptorSet set,
                            uint32_t binding, VkBuffer buffer,
                            VkDeviceSize range, VkDescriptorType type,
                            VkDeviceSize offset) {
  VkDescriptorBufferInfo bufferInfo{};
  bufferInfo.buffer = buffer;
  bufferInfo.offset = offset;
  bufferInfo.range = range;

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = binding;
  write.dstArrayElement = 0;
  write.descriptorType = type;
  write.descriptorCount = 1;
  write.pBufferInfo = &bufferInfo;

  vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
}

}  // namespace vkutil
