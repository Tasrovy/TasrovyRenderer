#pragma once

#include "glm/glm.hpp"
#include "glm/gtc/constants.hpp"
#include <cmath>
#include <initializer_list>

namespace Tasrovy {

    template <glm::length_t L, typename T>
    class TSVector : public glm::vec<L, T, glm::defaultp> {
    public:
        using Base = glm::vec<L, T, glm::defaultp>;
        using Base::Base;

        TSVector() = default;
        TSVector(const Base& v) : Base(v) {}
        TSVector(Base&& v) : Base(std::move(v)) {}
        TSVector& operator=(const Base& v) { Base::operator=(v); return *this; }
        TSVector& operator=(Base&& v) { Base::operator=(std::move(v)); return *this; }

        explicit operator Base&() { return *this; }
        explicit operator const Base&() const { return *this; }
    };

    // --- 常用别名 ---
    using TSVec2f = TSVector<2, float>;
    using TSVec3f = TSVector<3, float>;
    using TSVec4f = TSVector<4, float>;
    using TSVec2i = TSVector<2, int>;
    using TSVec3i = TSVector<3, int>;
    using TSVec4i = TSVector<4, int>;

    // --- 类型转换辅助 ---

    template <typename DstT, glm::length_t DstL, typename SrcT, glm::length_t SrcL>
    TSVector<DstL, DstT> vec_cast(const TSVector<SrcL, SrcT>& v) {
        TSVector<DstL, DstT> result;
        for (glm::length_t i = 0; i < DstL && i < SrcL; ++i) {
            result[i] = static_cast<DstT>(v[i]);
        }
        return result;
    }

    template <glm::length_t DstL, typename T>
    TSVector<DstL, T> truncate(const TSVector<DstL + 1, T>& v) {
        TSVector<DstL, T> result;
        for (glm::length_t i = 0; i < DstL; ++i) {
            result[i] = v[i];
        }
        return result;
    }

    // --- 便捷函数（纯手写，不依赖 glm 数学函数）---

    template <glm::length_t L, typename T>
    TSVector<L, T> zero() { return TSVector<L, T>(T(0)); }

    template <glm::length_t L, typename T>
    TSVector<L, T> one() { return TSVector<L, T>(T(1)); }

    template <typename T>
    TSVector<3, T> cross(const TSVector<3, T>& a, const TSVector<3, T>& b) {
        return TSVector<3, T>(
            a[1] * b[2] - a[2] * b[1],
            a[2] * b[0] - a[0] * b[2],
            a[0] * b[1] - a[1] * b[0]
        );
    }

    template <glm::length_t L, typename T>
    T dot(const TSVector<L, T>& a, const TSVector<L, T>& b) {
        T result = T(0);
        for (glm::length_t i = 0; i < L; ++i) {
            result += a[i] * b[i];
        }
        return result;
    }

    template <glm::length_t L, typename T>
    TSVector<L, T> normalize(const TSVector<L, T>& v) {
        T len = T(0);
        for (glm::length_t i = 0; i < L; ++i) {
            len += v[i] * v[i];
        }
        len = std::sqrt(len);
        if (len < T(1e-8)) {
            return zero<L, T>();
        }
        TSVector<L, T> result;
        for (glm::length_t i = 0; i < L; ++i) {
            result[i] = v[i] / len;
        }
        return result;
    }

    template <glm::length_t L, typename T>
    T length(const TSVector<L, T>& v) {
        T result = T(0);
        for (glm::length_t i = 0; i < L; ++i) {
            result += v[i] * v[i];
        }
        return std::sqrt(result);
    }

    template <glm::length_t L, typename T>
    T distance(const TSVector<L, T>& a, const TSVector<L, T>& b) {
        T result = T(0);
        for (glm::length_t i = 0; i < L; ++i) {
            T d = a[i] - b[i];
            result += d * d;
        }
        return std::sqrt(result);
    }

    template <glm::length_t L, typename T>
    TSVector<L, T> mix(const TSVector<L, T>& a, const TSVector<L, T>& b, T t) {
        TSVector<L, T> result;
        for (glm::length_t i = 0; i < L; ++i) {
            result[i] = a[i] + (b[i] - a[i]) * t;
        }
        return result;
    }

    template <glm::length_t L, typename T>
    TSVector<L, T> reflect(const TSVector<L, T>& I, const TSVector<L, T>& N) {
        T d = dot(I, N);
        return TSVector<L, T>(I - N * T(2) * d);
    }

    template <glm::length_t L, typename T>
    TSVector<L, T> abs(const TSVector<L, T>& v) {
        TSVector<L, T> result;
        for (glm::length_t i = 0; i < L; ++i) {
            result[i] = v[i] >= T(0) ? v[i] : -v[i];
        }
        return result;
    }

    template <glm::length_t L, typename T>
    TSVector<L, T> clamp(const TSVector<L, T>& v, T minVal, T maxVal) {
        TSVector<L, T> result;
        for (glm::length_t i = 0; i < L; ++i) {
            result[i] = v[i] < minVal ? minVal : (v[i] > maxVal ? maxVal : v[i]);
        }
        return result;
    }

    // --- 角度/常量 ---

    template <typename T>
    T radians(T degrees) {
        return degrees * static_cast<T>(3.14159265358979323846) / static_cast<T>(180);
    }

    template <typename T>
    T two_pi() {
        return static_cast<T>(3.14159265358979323846) * T(2);
    }

    template <typename T>
    T pi() {
        return static_cast<T>(3.14159265358979323846);
    }

} // namespace Tasrovy
