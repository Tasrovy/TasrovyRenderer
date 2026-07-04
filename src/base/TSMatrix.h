#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "TSVector.h"
#include <cmath>

namespace Tasrovy {

    template <glm::length_t C, glm::length_t R, typename T>
    class TSMatrix : public glm::mat<C, R, T, glm::defaultp> {
    public:
        using Base = glm::mat<C, R, T, glm::defaultp>;
        using Base::Base;

        TSMatrix() = default;
        TSMatrix(const Base& m) : Base(m) {}
        TSMatrix(Base&& m) : Base(std::move(m)) {}
        TSMatrix& operator=(const Base& m) { Base::operator=(m); return *this; }
        TSMatrix& operator=(Base&& m) { Base::operator=(std::move(m)); return *this; }
    };

    // --- 常用别名 ---
    using TSMat3f = TSMatrix<3, 3, float>;
    using TSMat4f = TSMatrix<4, 4, float>;

    // --- 矩阵操作函数 ---

    template <glm::length_t C, glm::length_t R, typename T>
    TSMatrix<C, R, T> transpose(const TSMatrix<C, R, T>& m) {
        TSMatrix<C, R, T> result;
        for (glm::length_t c = 0; c < C; ++c) {
            for (glm::length_t r = 0; r < R; ++r) {
                result[c][r] = m[r][c];
            }
        }
        return result;
    }

    template <glm::length_t C, glm::length_t R, typename T>
    TSMatrix<C, R, T> inverse(const TSMatrix<C, R, T>& m) {
        return TSMatrix<C, R, T>(glm::inverse(static_cast<typename TSMatrix<C, R, T>::Base>(m)));
    }

    template <typename T>
    TSMatrix<4, 4, T> translate(const TSMatrix<4, 4, T>& m, const TSVector<3, T>& v) {
        TSMatrix<4, 4, T> result = m;
        result[3] = m[0] * v[0] + m[1] * v[1] + m[2] * v[2] + m[3];
        return result;
    }

    template <typename T>
    TSMatrix<4, 4, T> rotate(const TSMatrix<4, 4, T>& m, T angle, const TSVector<3, T>& axis) {
        T c = std::cos(angle);
        T s = std::sin(angle);
        T t = T(1) - c;

        TSVector<3, T> n = normalize(axis);
        T x = n[0], y = n[1], z = n[2];

        TSMatrix<4, 4, T> r = TSMatrix<4, 4, T>(T(1));
        r[0][0] = t*x*x + c;       r[0][1] = t*x*y + s*z;   r[0][2] = t*x*z - s*y;
        r[1][0] = t*x*y - s*z;     r[1][1] = t*y*y + c;     r[1][2] = t*y*z + s*x;
        r[2][0] = t*x*z + s*y;     r[2][1] = t*y*z - s*x;   r[2][2] = t*z*z + c;

        return m * r;
    }

    template <typename T>
    TSMatrix<4, 4, T> scale(const TSMatrix<4, 4, T>& m, const TSVector<3, T>& v) {
        TSMatrix<4, 4, T> result;
        result[0] = m[0] * v[0];
        result[1] = m[1] * v[1];
        result[2] = m[2] * v[2];
        result[3] = m[3];
        return result;
    }

    template <typename T>
    TSMatrix<4, 4, T> perspective(T fovY, T aspect, T zNear, T zFar) {
        T tanHalf = std::tan(fovY / T(2));
        TSMatrix<4, 4, T> result = TSMatrix<4, 4, T>(T(0));
        result[0][0] = T(1) / (aspect * tanHalf);
        result[1][1] = T(1) / tanHalf;
        result[2][2] = -(zFar + zNear) / (zFar - zNear);
        result[2][3] = T(-1);
        result[3][2] = -(T(2) * zFar * zNear) / (zFar - zNear);
        return result;
    }

    template <typename T>
    TSMatrix<4, 4, T> lookAt(const TSVector<3, T>& eye,
                              const TSVector<3, T>& center,
                              const TSVector<3, T>& up) {
        TSVector<3, T> f = normalize(center - eye);
        TSVector<3, T> s = normalize(cross(f, up));
        TSVector<3, T> u = cross(s, f);

        TSMatrix<4, 4, T> result = TSMatrix<4, 4, T>(T(1));
        result[0][0] =  s[0];  result[0][1] =  u[0];  result[0][2] = -f[0];
        result[1][0] =  s[1];  result[1][1] =  u[1];  result[1][2] = -f[1];
        result[2][0] =  s[2];  result[2][1] =  u[2];  result[2][2] = -f[2];
        result[3][0] = -dot(s, eye);
        result[3][1] = -dot(u, eye);
        result[3][2] =  dot(f, eye);
        return result;
    }

    // --- 子矩阵提取 ---

    template <glm::length_t DstC, glm::length_t DstR, typename T, glm::length_t SrcC, glm::length_t SrcR>
    TSMatrix<DstC, DstR, T> mat_cast(const TSMatrix<SrcC, SrcR, T>& m) {
        TSMatrix<DstC, DstR, T> result;
        for (glm::length_t c = 0; c < DstC && c < SrcC; ++c) {
            for (glm::length_t r = 0; r < DstR && r < SrcR; ++r) {
                result[c][r] = m[c][r];
            }
        }
        return result;
    }

} // namespace Tasrovy
