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

if(TASROVY_ENABLE_DLSS_NR)
    set(_tasrovy_ngx_header
        "${TASROVY_NGX_SDK_ROOT}/include/nvsdk_ngx_vk.h")
    set(_tasrovy_dlss_nr_runtime
        "${TASROVY_DLSS_NR_RUNTIME_DIR}/nvngx_dlssnr.dll")

    if(NOT EXISTS "${_tasrovy_ngx_header}")
        message(FATAL_ERROR
            "TASROVY_NGX_SDK_ROOT does not contain include/nvsdk_ngx_vk.h")
    endif()
    if(NOT EXISTS "${_tasrovy_dlss_nr_runtime}")
        message(FATAL_ERROR
            "TASROVY_DLSS_NR_RUNTIME_DIR does not contain nvngx_dlssnr.dll")
    endif()

    file(TO_CMAKE_PATH "${TASROVY_DLSS_NR_RUNTIME_DIR}"
        TASROVY_DLSS_NR_RUNTIME_DIR_NORMALIZED)
    configure_file(
        TasrovyDlssNrConfig.h.in
        "${CMAKE_CURRENT_BINARY_DIR}/generated/TasrovyDlssNrConfig.h"
        @ONLY)

    add_library(TasrovyDlssNrVulkanBridge SHARED
        VulkanDlssNrBridge.cpp)
    set_target_properties(TasrovyDlssNrVulkanBridge PROPERTIES
        PREFIX ""
        OUTPUT_NAME "nvngx.dll_dlssnr_vulkan"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/ngx")
    target_include_directories(TasrovyDlssNrVulkanBridge PRIVATE
        "${TASROVY_NGX_SDK_ROOT}/include")
    target_link_libraries(TasrovyDlssNrVulkanBridge PRIVATE volk::volk)

    target_sources(TasrovyRHI PRIVATE
        VulkanDlssNrExecutor.cpp
        VulkanDlssNrExecutor.h)
    target_include_directories(TasrovyRHI PRIVATE
        "${TASROVY_NGX_SDK_ROOT}/include"
        "${CMAKE_CURRENT_BINARY_DIR}/generated")
    target_compile_definitions(TasrovyRHI PRIVATE
        TASROVY_ENABLE_DLSS_NR=1
        TASROVY_ALLOW_UNTRUSTED_DLSS_NR_RUNTIME=$<BOOL:${TASROVY_ALLOW_UNTRUSTED_DLSS_NR_RUNTIME}>)
    target_link_libraries(TasrovyRHI PRIVATE advapi32)
    if(NOT TASROVY_ALLOW_UNTRUSTED_DLSS_NR_RUNTIME)
        message(WARNING
            "DLSS-NR is built but runtime loading is disabled because the configured DLL has not been trusted explicitly")
    endif()
endif()
