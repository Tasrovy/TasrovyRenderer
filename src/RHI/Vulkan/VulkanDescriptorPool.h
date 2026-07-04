#pragma once
#include "VulkanContext.h"
#include "VulkanDescriptorSetLayout.h"
#include <vector>
#include <memory>
#include <map>

class VulkanDescriptorPool {
public:
    class Builder {
    public:
        Builder(VulkanContext& context);

        Builder& addPoolSize(VkDescriptorType descriptorType, uint32_t count);
        Builder& setMaxSets(uint32_t count);
        Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
        
        std::unique_ptr<VulkanDescriptorPool> build() const;

    private:
        VulkanContext& _context;
        std::vector<VkDescriptorPoolSize> _poolSizes;
        uint32_t _maxSets = 1000;
        VkDescriptorPoolCreateFlags _poolFlags = 0;
    };

    VulkanDescriptorPool(VulkanContext& context, VkDescriptorPool pool);
    ~VulkanDescriptorPool();

    // 禁止拷贝
    VulkanDescriptorPool(const VulkanDescriptorPool&) = delete;
    VulkanDescriptorPool& operator=(const VulkanDescriptorPool&) = delete;

    // 从池中分配一个描述符集
    // 注意：返回的是原始 VkDescriptorSet 句柄，其生命周期与池绑定
    VkDescriptorSet allocateSet(const VulkanDescriptorSetLayout& setLayout);

    // 释放一个描述符集（需要池在创建时设置 VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT）
    void freeSet(VkDescriptorSet descriptorSet);

private:
    VulkanContext& _context;
    VkDescriptorPool _descriptorPool;
};