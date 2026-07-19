#pragma once

#include "Transform.h"
#include <memory>
#include <string>

namespace Tasrovy::Render {

class Camera {
public:
    static std::unique_ptr<Camera> create(const std::string& name = "");
    static std::unique_ptr<Camera> create(TSVec3f position, TSVec3f rotation,
                                          float fov = 45.0f, float aspect = 16.0f / 9.0f,
                                          float nearPlane = 0.1f, float farPlane = 100.0f,
                                          const std::string& name = "");
    std::unique_ptr<Camera> clone() const;

    void setName(const std::string& name);
    const std::string& getName() const;

    Transform& getTransform();
    const Transform& getTransform() const;

    void setPosition(TSVec3f pos);
    void setRotation(TSVec3f euler);
    void setRotation(TSQuatf quat);
    void setFOV(float fov);
    void setAspect(float aspect);
    void setNearPlane(float near);
    void setFarPlane(float far);

    TSVec3f getPosition() const;
    TSVec3f getRotationEuler() const;
    TSQuatf getRotationQuat() const;
    float getFOV() const;
    float getAspect() const;
    float getNearPlane() const;
    float getFarPlane() const;

    TSMat4f getViewMatrix() const;
    TSMat4f getProjectionMatrix() const;

private:
    Camera();
    Camera(TSVec3f position, TSVec3f rotation,
           float fov, float aspect, float nearPlane, float farPlane);

    std::string name_;
    Transform transform_;
    float fov_;
    float aspect_;
    float nearPlane_;
    float farPlane_;
};

} // namespace Tasrovy::Render
