#pragma once
#include "VulkanContext.h"
#include <vector>
#include <memory>
#include <map>

class VulkanDescriptorSetLayout {
public:
    class Builder {
    public:
        Builder(VulkanContext& context);

        // 添加一个绑定到布局中
        Builder& addBinding(uint32_t binding, VkDescriptorType descriptorType, VkShaderStageFlags stageFlags, uint32_t count = 1);
        
        // 构建最终的 DescriptorSetLayout 对象
        std::unique_ptr<VulkanDescriptorSetLayout> build() const;

    private:
        VulkanContext& _context;
        std::map<uint32_t, VkDescriptorSetLayoutBinding> _bindings{};
    };

    VulkanDescriptorSetLayout(VulkanContext& context, VkDescriptorSetLayout layout);
    ~VulkanDescriptorSetLayout();

    // 禁止拷贝
    VulkanDescriptorSetLayout(const VulkanDescriptorSetLayout&) = delete;
    VulkanDescriptorSetLayout& operator=(const VulkanDescriptorSetLayout&) = delete;

    VkDescriptorSetLayout getLayout() const { return _layout; }

private:
    VulkanContext& _context;
    VkDescriptorSetLayout _layout;
};