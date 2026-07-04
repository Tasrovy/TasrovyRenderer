#include "Camera.h"

namespace Tasrovy {

std::unique_ptr<Camera> Camera::create(const std::string& name) {
    auto cam = std::unique_ptr<Camera>(new Camera());
    cam->name_ = name;
    return cam;
}

std::unique_ptr<Camera> Camera::create(TSVec3f position, TSVec3f rotation,
                                        float fov, float aspect, float nearPlane, float farPlane,
                                        const std::string& name) {
    auto cam = std::unique_ptr<Camera>(new Camera(position, rotation, fov, aspect, nearPlane, farPlane));
    cam->name_ = name;
    return cam;
}

Camera::Camera()
    : transform_(TSVec3f(0.0f, 1.0f, 5.0f))
    , fov_(45.0f)
    , aspect_(16.0f / 9.0f)
    , nearPlane_(0.1f)
    , farPlane_(100.0f) {
}

Camera::Camera(TSVec3f position, TSVec3f rotation, float fov, float aspect,
               float nearPlane, float farPlane)
    : transform_(position, rotation)
    , fov_(fov)
    , aspect_(aspect)
    , nearPlane_(nearPlane)
    , farPlane_(farPlane) {
}

void Camera::setName(const std::string& name) { name_ = name; }
const std::string& Camera::getName() const { return name_; }

Transform& Camera::getTransform() { return transform_; }
const Transform& Camera::getTransform() const { return transform_; }

void Camera::setPosition(TSVec3f pos) { transform_.setPosition(pos); }
void Camera::setRotation(TSVec3f euler) { transform_.setRotation(euler); }
void Camera::setRotation(TSQuatf quat) { transform_.setRotation(quat); }
void Camera::setFOV(float fov) { fov_ = fov; }
void Camera::setAspect(float aspect) { aspect_ = aspect; }
void Camera::setNearPlane(float near) { nearPlane_ = near; }
void Camera::setFarPlane(float far) { farPlane_ = far; }

TSVec3f Camera::getPosition() const { return transform_.getPosition(); }
TSVec3f Camera::getRotationEuler() const { return transform_.getRotationEuler(); }
TSQuatf Camera::getRotationQuat() const { return transform_.getRotationQuat(); }
float Camera::getFOV() const { return fov_; }
float Camera::getAspect() const { return aspect_; }

TSMat4f Camera::getProjectionMatrix() const {
    return perspective(radians(fov_), aspect_, nearPlane_, farPlane_);
}

TSMat4f Camera::getViewMatrix() const {
    return inverse(transform_.getModelMatrix());
}

} // namespace Tasrovy
