#include "VulkanDescriptorSetLayout.h"
#include <stdexcept>

// --- Builder Implementation ---

VulkanDescriptorSetLayout::Builder::Builder(VulkanContext& context) : _context(context) {}

VulkanDescriptorSetLayout::Builder& VulkanDescriptorSetLayout::Builder::addBinding(
    uint32_t binding,
    VkDescriptorType descriptorType,
    VkShaderStageFlags stageFlags,
    uint32_t count)
{
    // 确保绑定点没有被重复使用
    if (_bindings.count(binding)) {
        throw std::runtime_error("Binding " + std::to_string(binding) + " is already in use.");
    }

    VkDescriptorSetLayoutBinding layoutBinding{};
    layoutBinding.binding = binding;
    layoutBinding.descriptorType = descriptorType;
    layoutBinding.descriptorCount = count;
    layoutBinding.stageFlags = stageFlags;
    layoutBinding.pImmutableSamplers = nullptr; // 可选：对于采样器，可以设置不可变采样器

    _bindings[binding] = layoutBinding;

    return *this;
}

std::unique_ptr<VulkanDescriptorSetLayout> VulkanDescriptorSetLayout::Builder::build() const {
    std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
    for (auto const& [binding, layoutBinding] : _bindings) {
        setLayoutBindings.push_back(layoutBinding);
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
    layoutInfo.pBindings = setLayoutBindings.data();

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(_context.getDevice(), &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor set layout!");
    }

    return std::make_unique<VulkanDescriptorSetLayout>(_context, layout);
}


// --- VulkanDescriptorSetLayout Implementation ---

VulkanDescriptorSetLayout::VulkanDescriptorSetLayout(VulkanContext& context, VkDescriptorSetLayout layout)
    : _context(context), _layout(layout) {
}

VulkanDescriptorSetLayout::~VulkanDescriptorSetLayout() {
    vkDestroyDescriptorSetLayout(_context.getDevice(), _layout, nullptr);
}