#pragma once

#include "Object.h"

namespace Tasrovy {

class Texture;

class Skybox : public Object {
public:
    static std::shared_ptr<Skybox> create(const std::string& name = "Skybox");

    // Skybox 忽略自身 transform，model matrix 恒为单位矩阵
    TSMat4f getModelMatrix() const override;

    // 从相机 view matrix 中剔除平移分量，只保留旋转
    // 用于 vertex shader 中将 skybox 渲染在无穷远处
    static TSMat4f getSkyViewMatrix(const TSMat4f& cameraView);

    void setCubemap(std::shared_ptr<Texture> cubemap);
    std::shared_ptr<Texture> getCubemap() const;

private:
    Skybox();
    explicit Skybox(const std::string& name);

    std::shared_ptr<Texture> cubemap_;
};

} // namespace Tasrovy
