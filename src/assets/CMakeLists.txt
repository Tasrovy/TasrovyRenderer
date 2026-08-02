add_library(TasrovyAssets STATIC
        RenderAssetFactory.cpp
        RenderAssetFactory.h
)

target_include_directories(TasrovyAssets PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(TasrovyAssets PUBLIC
        TasrovyFileSystem
        TasrovyRender
)
