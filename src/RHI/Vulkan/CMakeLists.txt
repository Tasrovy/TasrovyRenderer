find_package(volk CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)

add_library(TasrovyRHI_Vulkan STATIC
        ../Buffer.cpp
        ../CommandList.cpp
        ../CompiledRenderPipeline.cpp
        ../Descriptor.cpp
        ../Device.cpp
        ../FrameExecutor.cpp
        ../FrameScheduler.cpp
        ../Image.cpp
        ../Pass.cpp
        ../Pipeline.cpp
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
        VulkanContext.cpp
        VulkanContext.h
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
        VulkanRenderFramePlan.cpp
        VulkanRenderFramePlan.h
        VulkanFrameExecutor.cpp
        VulkanFrameExecutor.h
        VulkanSwapChain.cpp
        VulkanSwapChain.h
)

target_include_directories(TasrovyRHI_Vulkan PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

target_link_libraries(TasrovyRHI_Vulkan PUBLIC
        volk::volk
        glm::glm
        TasrovyBase
        TasrovyLog
        glfw
)
