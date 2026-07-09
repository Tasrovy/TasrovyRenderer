#pragma once

#include "TSVector.h"
#include "TSMatrix.h"
#include "glm/gtc/quaternion.hpp"
#define GLM_ENABLE_EXPERIMENTAL
#include "glm/gtx/quaternion.hpp"

namespace Tasrovy::Base {

template <typename T>
class TSQuaternion : public glm::tquat<T, glm::defaultp> {
public:
    using Base = glm::tquat<T, glm::defaultp>;
    using Base::Base;

    TSQuaternion() = default;
    TSQuaternion(const Base& q) : Base(q) {}
    TSQuaternion(Base&& q) : Base(std::move(q)) {}
    TSQuaternion& operator=(const Base& q) { Base::operator=(q); return *this; }
    TSQuaternion& operator=(Base&& q) { Base::operator=(std::move(q)); return *this; }

    explicit operator Base&() { return *this; }
    explicit operator const Base&() const { return *this; }
};

using TSQuatf = TSQuaternion<float>;
using TSQuatd = TSQuaternion<double>;

// --- 构�?---

template <typename T>
TSQuaternion<T> quatFromAxisAngle(TSVector<3, T> axis, T angle) {
    return TSQuaternion<T>(glm::angleAxis(angle, glm::vec<3, T>(axis)));
}

template <typename T>
TSQuaternion<T> quatFromEuler(T eulerX, T eulerY, T eulerZ) {
    return TSQuaternion<T>(glm::quat(glm::vec<3, T>(eulerX, eulerY, eulerZ)));
}

template <typename T>
TSQuaternion<T> quatFromEuler(TSVector<3, T> euler) {
    return quatFromEuler<T>(euler.x, euler.y, euler.z);
}

// --- 基本操作 ---

template <typename T>
TSQuaternion<T> conjugate(const TSQuaternion<T>& q) {
    return TSQuaternion<T>(glm::conjugate(q));
}

template <typename T>
TSQuaternion<T> inverse(const TSQuaternion<T>& q) {
    return TSQuaternion<T>(glm::inverse(q));
}

template <typename T>
T length(const TSQuaternion<T>& q) {
    return glm::length(q);
}

template <typename T>
TSQuaternion<T> normalize(const TSQuaternion<T>& q) {
    return TSQuaternion<T>(glm::normalize(q));
}

// --- 插�?---

template <typename T>
TSQuaternion<T> slerp(const TSQuaternion<T>& a, const TSQuaternion<T>& b, T t) {
    return TSQuaternion<T>(glm::slerp(a, b, t));
}

// --- 旋转操作 ---

template <typename T>
TSQuaternion<T> rotate(const TSQuaternion<T>& q, T angle, TSVector<3, T> axis) {
    return TSQuaternion<T>(glm::rotate(q, angle, glm::vec<3, T>(axis)));
}

template <typename T>
TSVector<3, T> rotate(const TSQuaternion<T>& q, TSVector<3, T> v) {
    return TSVector<3, T>(glm::rotate(q, glm::vec<3, T>(v)));
}

// --- 转换 ---

template <typename T>
TSMatrix<4, 4, T> mat4_cast(const TSQuaternion<T>& q) {
    return TSMatrix<4, 4, T>(glm::mat4_cast(q));
}

template <typename T>
TSMatrix<3, 3, T> mat3_cast(const TSQuaternion<T>& q) {
    return TSMatrix<3, 3, T>(glm::mat3_cast(q));
}

template <typename T>
TSQuaternion<T> quat_cast(const TSMatrix<3, 3, T>& m) {
    return TSQuaternion<T>(glm::quat_cast(m));
}

template <typename T>
TSQuaternion<T> quat_cast(const TSMatrix<4, 4, T>& m) {
    return TSQuaternion<T>(glm::quat_cast(m));
}

template <typename T>
TSVector<3, T> eulerAngles(const TSQuaternion<T>& q) {
    return TSVector<3, T>(glm::eulerAngles(q));
}

template <typename T>
T yaw(const TSQuaternion<T>& q) { return glm::yaw(q); }

template <typename T>
T pitch(const TSQuaternion<T>& q) { return glm::pitch(q); }

template <typename T>
T roll(const TSQuaternion<T>& q) { return glm::roll(q); }

} // namespace Tasrovy::Base

namespace Tasrovy { using namespace Base; }
