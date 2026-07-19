#pragma once

#include "Object.h"
#include <cstdint>
#include <memory>
#include <string>

namespace Tasrovy::Render {

class Plane final : public Object {
public:
    static std::shared_ptr<Plane> create(const std::string& name = "Plane");
    std::shared_ptr<Object> clone() const override;

private:
    explicit Plane(const std::string& name);
};

class Cube final : public Object {
public:
    static std::shared_ptr<Cube> create(const std::string& name = "Cube");
    std::shared_ptr<Object> clone() const override;

private:
    explicit Cube(const std::string& name);
};

class Sphere final : public Object {
public:
    static std::shared_ptr<Sphere> create(
        const std::string& name = "Sphere",
        uint32_t sectors = 32,
        uint32_t stacks = 16);
    std::shared_ptr<Object> clone() const override;
    uint32_t getSectors() const { return sectors_; }
    uint32_t getStacks() const { return stacks_; }

private:
    Sphere(const std::string& name, uint32_t sectors, uint32_t stacks);

    uint32_t sectors_ = 32;
    uint32_t stacks_ = 16;
};

} // namespace Tasrovy::Render
