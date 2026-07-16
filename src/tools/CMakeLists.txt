add_executable(TasrovyAssetInspect
        AssetInspect.cpp
)

target_link_libraries(TasrovyAssetInspect PRIVATE
        TasrovyFileSystem
        TasrovyLog
        assimp::assimp
)
