#pragma once

#include <memory>
#include <string>

#include "TSVector.h"

namespace Tasrovy {

class Light {
public:
    virtual ~Light();

    void setName(const std::string& name);
    const std::string& getName() const;

    void setDirection(TSVec3f dir);
    void setColor(TSVec3f color);
    void setIntensity(float intensity);

    TSVec3f getDirection() const;
    TSVec3f getColor() const;
    float getIntensity() const;

protected:
    Light() = default;
    Light(const std::string& name, TSVec3f direction, TSVec3f color, float intensity);

    std::string name_;
    TSVec3f direction_;
    TSVec3f color_;
    float intensity_ = 1.0f;
};

class DirectionalLight : public Light {
public:
    static std::unique_ptr<DirectionalLight> create(TSVec3f direction, TSVec3f color, float intensity,
                                                     const std::string& name = "");

private:
    DirectionalLight() = default;
    DirectionalLight(const std::string& name, TSVec3f direction, TSVec3f color, float intensity);
};

class PointLight : public Light {
public:
    static std::unique_ptr<PointLight> create(TSVec3f position, TSVec3f color, float intensity,
                                               float constant = 1.0f, float linear = 0.09f, float quadratic = 0.032f,
                                               const std::string& name = "");

    void setPosition(TSVec3f pos);
    TSVec3f getPosition() const;

    void setConstant(float c);
    void setLinear(float l);
    void setQuadratic(float q);
    float getConstant() const;
    float getLinear() const;
    float getQuadratic() const;

private:
    PointLight() = default;
    PointLight(const std::string& name, TSVec3f position, TSVec3f color, float intensity,
               float constant, float linear, float quadratic);

    TSVec3f position_;
    float constant_ = 1.0f;
    float linear_ = 0.09f;
    float quadratic_ = 0.032f;
};

class SpotLight : public Light {
public:
    static std::unique_ptr<SpotLight> create(TSVec3f position, TSVec3f direction, TSVec3f color,
                                               float intensity, float cutoff = 12.5f,
                                               const std::string& name = "");

    void setPosition(TSVec3f pos);
    void setCutoff(float degrees);
    TSVec3f getPosition() const;
    float getCutoff() const;

private:
    SpotLight() = default;
    SpotLight(const std::string& name, TSVec3f position, TSVec3f direction, TSVec3f color,
              float intensity, float cutoff);

    TSVec3f position_;
    float cutoff_ = 12.5f;
};

} // namespace Tasrovy
