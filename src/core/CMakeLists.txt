add_executable(TasrovyCore
        main.cpp
        SceneSerializer.cpp
        SceneSerializer.h
)

find_package(nlohmann_json CONFIG REQUIRED)

target_include_directories(TasrovyCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyCore PUBLIC
        TasrovyWindow
        TasrovyUI
        TasrovyRendererRuntime
        TasrovyRHI
        TasrovyFileSystem
        TasrovyAssets
        TasrovyRender
        TasrovyLog
        TasrovyExt
        imgui
        nlohmann_json::nlohmann_json
)

if(TARGET TasrovyDlssNrVulkanBridge)
    add_dependencies(TasrovyCore TasrovyDlssNrVulkanBridge)
    add_custom_command(TARGET TasrovyCore POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:TasrovyDlssNrVulkanBridge>
            $<TARGET_FILE_DIR:TasrovyCore>
        VERBATIM)
endif()
