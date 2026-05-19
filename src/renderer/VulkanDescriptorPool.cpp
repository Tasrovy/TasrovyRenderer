#include "VulkanDescriptorPool.h"
#include <stdexcept>

// --- Builder Implementation ---
VulkanDescriptorPool::Builder::Builder(VulkanContext& context) : _context(context) {}

VulkanDescriptorPool::Builder& VulkanDescriptorPool::Builder::addPoolSize(VkDescriptorType descriptorType, uint32_t count) {
    _poolSizes.push_back({ descriptorType, count });
    return *this;
}

VulkanDescriptorPool::Builder& VulkanDescriptorPool::Builder::setMaxSets(uint32_t count) {
    _maxSets = count;
    return *this;
}

VulkanDescriptorPool::Builder& VulkanDescriptorPool::Builder::setPoolFlags(VkDescriptorPoolCreateFlags flags) {
    _poolFlags = flags;
    return *this;
}

std::unique_ptr<VulkanDescriptorPool> VulkanDescriptorPool::Builder::build() const {
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = _poolFlags;
    poolInfo.maxSets = _maxSets;
    poolInfo.poolSizeCount = static_cast<uint32_t>(_poolSizes.size());
    poolInfo.pPoolSizes = _poolSizes.data();

    VkDescriptorPool descriptorPool;
    if (vkCreateDescriptorPool(_context.getDevice(), &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor pool!");
    }

    return std::make_unique<VulkanDescriptorPool>(_context, descriptorPool);
}

// --- VulkanDescriptorPool Implementation ---
VulkanDescriptorPool::VulkanDescriptorPool(VulkanContext& context, VkDescriptorPool pool)
    : _context(context), _descriptorPool(pool) {
}

VulkanDescriptorPool::~VulkanDescriptorPool() {
    vkDestroyDescriptorPool(_context.getDevice(), _descriptorPool, nullptr);
}

VkDescriptorSet VulkanDescriptorPool::allocateSet(const VulkanDescriptorSetLayout& setLayout) {
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = _descriptorPool;
    allocInfo.descriptorSetCount = 1;
    VkDescriptorSetLayout layout = setLayout.getLayout();
    allocInfo.pSetLayouts = &layout;

    VkDescriptorSet descriptorSet;
    if (vkAllocateDescriptorSets(_context.getDevice(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("failed to allocate descriptor set!");
    }
    return descriptorSet;
}

void VulkanDescriptorPool::freeSet(VkDescriptorSet descriptorSet) {
    vkFreeDescriptorSets(_context.getDevice(), _descriptorPool, 1, &descriptorSet);
}