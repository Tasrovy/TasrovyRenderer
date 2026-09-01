target_sources(TasrovyRHI PRIVATE
    DescriptorWriter.cpp
    DescriptorWriter.h
    IBLMap.cpp
    IBLMap.h
    ImmediateSubmitter.cpp
    ImmediateSubmitter.h
    Renderer.cpp
    Renderer.h
    VulkanBuffer.cpp
    VulkanBuffer.h
    VulkanBackendFactory.cpp
    VulkanContext.cpp
    VulkanContext.h
    VulkanConversions.cpp
    VulkanConversions.h
    VulkanCommandListBackend.cpp
    VulkanCommandListBackend.h
    VulkanDeviceBackend.cpp
    VulkanDeviceBackend.h
    VulkanDescriptorPool.cpp
    VulkanDescriptorPool.h
    VulkanDescriptorSetLayout.cpp
    VulkanDescriptorSetLayout.h
    VulkanImage.cpp
    VulkanImage.h
    VulkanPipeline.cpp
    VulkanPipeline.h
    VulkanQueue.cpp
    VulkanQueue.h
    VulkanResourceBackends.cpp
    VulkanResourceBackends.h
    VulkanRenderFramePlan.cpp
    VulkanRenderFramePlan.h
    VulkanFrameExecutor.cpp
    VulkanFrameExecutor.h
    VulkanFrameSchedulerBackend.cpp
    VulkanFrameSchedulerBackend.h
    VulkanRenderOverlayBackend.h
    VulkanShaderBinary.cpp
    VulkanShaderBinary.h
    VulkanSwapChain.cpp
    VulkanSwapChain.h
)

target_include_directories(TasrovyRHI PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(TasrovyRHI PUBLIC
    volk::volk
    glm::glm
    TasrovyLog
    glfw
)
