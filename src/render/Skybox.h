#pragma once

#include "Object.h"

namespace Tasrovy::Render {

class Texture;

class Skybox : public Object {
public:
    static std::shared_ptr<Skybox> create(const std::string& name = "Skybox");
    std::shared_ptr<Object> clone() const override;

    // Skybox 忽略自身 transform，model matrix 恒为单位矩阵
    TSMat4f getModelMatrix() const override;

    // Remove camera translation and keep only rotation for skybox rendering.
    static TSMat4f getSkyViewMatrix(const TSMat4f& cameraView);

    void setCubemap(std::shared_ptr<Texture> cubemap);
    std::shared_ptr<Texture> getCubemap() const;

private:
    Skybox();
    explicit Skybox(const std::string& name);

    std::shared_ptr<Texture> cubemap_;
};

} // namespace Tasrovy::Render
