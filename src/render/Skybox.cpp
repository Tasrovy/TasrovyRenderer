#include "Skybox.h"
#include "Texture.hpp"

namespace Tasrovy {

std::shared_ptr<Skybox> Skybox::create(const std::string& name) {
    auto sky = std::shared_ptr<Skybox>(new Skybox(name));
    return sky;
}

Skybox::Skybox()
    : Object("Skybox") {
}

Skybox::Skybox(const std::string& name)
    : Object(name) {
}

TSMat4f Skybox::getModelMatrix() const {
    return TSMat4f(1.0f);
}

TSMat4f Skybox::getSkyViewMatrix(const TSMat4f& cameraView) {
    // 提取左上 3x3（旋转部分），重新构造 4x4 单位矩阵
    TSMat4f skyView = TSMat4f(1.0f);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            skyView[c][r] = cameraView[c][r];
        }
    }
    return skyView;
}

void Skybox::setCubemap(std::shared_ptr<Texture> cubemap) { cubemap_ = std::move(cubemap); }
std::shared_ptr<Texture> Skybox::getCubemap() const { return cubemap_; }

} // namespace Tasrovy
