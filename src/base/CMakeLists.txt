add_library(TasrovyBase STATIC
        TSVector.h
        TSVector.cpp
        TSMatrix.cpp
        TSMatrix.h
        TSQuaternion.cpp
        TSQuaternion.h
)

target_include_directories(TasrovyBase PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(TasrovyBase PUBLIC
        glm::glm
        assimp::assimp
        TasrovyExt
        TasrovyLog
)