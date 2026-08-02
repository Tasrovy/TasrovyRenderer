#pragma once

#include "TSVector.h"
#include "TSMatrix.h"
#include "TSQuaternion.h"

namespace Tasrovy::Render {

class Transform {
public:
    Transform() = default;
    Transform(TSVec3f position, TSVec3f rotation = TSVec3f(0.0f),
              TSVec3f scale = TSVec3f(1.0f));
    Transform(TSVec3f position, TSQuatf rotation, TSVec3f scale = TSVec3f(1.0f));

    void setPosition(TSVec3f pos);
    void setRotation(TSVec3f euler);
    void setRotation(TSQuatf quat);
    void setScale(TSVec3f s);

    TSVec3f getPosition() const;
    TSVec3f getRotationEuler() const;
    TSQuatf getRotationQuat() const;
    TSVec3f getScale() const;

    TSMat4f getModelMatrix() const;

private:
    TSVec3f position_ = TSVec3f(0.0f);
    TSQuatf rotation_ = TSQuatf(1.0f, 0.0f, 0.0f, 0.0f);
    TSVec3f scale_ = TSVec3f(1.0f);
};

} // namespace Tasrovy::Render
