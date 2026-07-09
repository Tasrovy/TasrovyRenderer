#include "Pipeline.h"
#include "RHIConfig.h"

#ifdef TASROVY_API_VULKAN
    #include "../RHI/Vulkan/VulkanPipeline.h"
#endif

namespace Tasrovy::RHI {

struct Pipeline::Impl {
#ifdef TASROVY_API_VULKAN
    std::unique_ptr<VulkanPipeline> vkPipeline;
#endif
};

Pipeline::~Pipeline() = default;

std::shared_ptr<Pipeline> Pipeline::CreateFromNative(void* nativePipeline) {
    auto pipeline = std::shared_ptr<Pipeline>(new Pipeline());
    pipeline->impl_ = std::make_unique<Impl>();
#ifdef TASROVY_API_VULKAN
    pipeline->impl_->vkPipeline.reset(static_cast<VulkanPipeline*>(nativePipeline));
#endif
    return pipeline;
}

uint64_t Pipeline::getNativePipeline() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkPipeline->getPipeline());
#else
    return 0;
#endif
}

uint64_t Pipeline::getNativeLayout() const {
#ifdef TASROVY_API_VULKAN
    return reinterpret_cast<uint64_t>(impl_->vkPipeline->getLayout());
#else
    return 0;
#endif
}

} // namespace Tasrovy::RHI
