#include "Descriptor.h"
#include "RHIConfig.h"

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanDescriptorPool.h"
    #include "../RHI/Vulkan/VulkanDescriptorSetLayout.h"
#endif

namespace Tasrovy::RHI {

struct DescriptorSetLayout::Impl {
#ifdef TASROVY_API_VULKAN
    std::unique_ptr<VulkanDescriptorSetLayout> vkLayout;
#endif
};

struct DescriptorPool::Impl {
#ifdef TASROVY_API_VULKAN
    std::unique_ptr<VulkanDescriptorPool> vkPool;
#endif
};

DescriptorSetLayout::~DescriptorSetLayout() = default;
DescriptorPool::~DescriptorPool() = default;

std::shared_ptr<DescriptorSetLayout> DescriptorSetLayout::CreateFromNative(void* nativeLayout) {
    auto layout = std::shared_ptr<DescriptorSetLayout>(new DescriptorSetLayout());
    layout->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    layout->impl_->vkLayout.reset(static_cast<VulkanDescriptorSetLayout*>(nativeLayout));
#endif
    return layout;
}

uint64_t DescriptorSetLayout::getNativeLayout() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkLayout->getLayout());
#else
    return 0;
#endif
}

std::shared_ptr<DescriptorPool> DescriptorPool::CreateFromNative(void* nativePool) {
    auto pool = std::shared_ptr<DescriptorPool>(new DescriptorPool());
    pool->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    pool->impl_->vkPool.reset(static_cast<VulkanDescriptorPool*>(nativePool));
#endif
    return pool;
}

DescriptorSet DescriptorPool::allocateSet(const DescriptorSetLayout& layout) {
#ifdef TASROVY_API_VULKAN
    auto* vkLayout = layout.impl_->vkLayout.get();
    return DescriptorSet(impl_->vkPool->allocateSet(*vkLayout));
#else
    return {};
#endif
}

} // namespace Tasrovy::RHI
