find_package(volk CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

add_library(TasrovyRHI_Vulkan STATIC
        DescriptorWriter.cpp
        DescriptorWriter.h
        IBLMap.cpp
        IBLMap.h
        ImmediateSubmitter.cpp
        ImmediateSubmitter.h
        Model.cpp
        Model.h
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
        VulkanSwapChain.cpp
        VulkanSwapChain.h
        Dependencies.h
)

target_include_directories(TasrovyRHI_Vulkan PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyRHI_Vulkan PUBLIC
        volk::volk
        Vulkan::Vulkan
        glm::glm
        TasrovyExt
        TasrovyLog
)
