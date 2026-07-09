#include "Transform.h"

namespace Tasrovy::Render {

Transform::Transform(TSVec3f position, TSVec3f rotation, TSVec3f scale)
    : position_(position)
    , rotation_(quatFromEuler(rotation))
    , scale_(scale) {
}

Transform::Transform(TSVec3f position, TSQuatf rotation, TSVec3f scale)
    : position_(position)
    , rotation_(rotation)
    , scale_(scale) {
}

void Transform::setPosition(TSVec3f pos) { position_ = pos; }
void Transform::setRotation(TSVec3f euler) { rotation_ = quatFromEuler(euler); }
void Transform::setRotation(TSQuatf quat) { rotation_ = quat; }
void Transform::setScale(TSVec3f s) { scale_ = s; }

TSVec3f Transform::getPosition() const { return position_; }
TSVec3f Transform::getRotationEuler() const { return eulerAngles(rotation_); }
TSQuatf Transform::getRotationQuat() const { return rotation_; }
TSVec3f Transform::getScale() const { return scale_; }

TSMat4f Transform::getModelMatrix() const {
    TSMat4f model = TSMat4f(1.0f);
    model = translate(model, position_);
    model = model * mat4_cast(rotation_);
    model = scale(model, scale_);
    return model;
}

} // namespace Tasrovy::Render
