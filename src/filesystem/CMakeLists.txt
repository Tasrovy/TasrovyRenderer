find_package(glm CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(Taskflow CONFIG REQUIRED)

add_library(TasrovyFileSystem STATIC
        Anim.cpp
        Anim.hpp
        AssetManager.cpp
        AssetManager.hpp
        AssetLoader.cpp
        AssetLoader.hpp
        FileSystem.cpp
        FileSystem.hpp
        Image.cpp
        Image.hpp
        Model.cpp
        Model.hpp
)

target_include_directories(TasrovyFileSystem PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyFileSystem PUBLIC
        glm::glm
        assimp::assimp
        Taskflow::Taskflow
        TasrovyExt
        TasrovyLog
        TasrovyBase
)
