#pragma once

#include <memory>
#include <string>

#include "TSVector.h"

namespace Tasrovy::Render {

class Light {
public:
    virtual ~Light();
    virtual std::unique_ptr<Light> clone() const = 0;

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
    std::unique_ptr<Light> clone() const override;

private:
    DirectionalLight() = default;
    DirectionalLight(const std::string& name, TSVec3f direction, TSVec3f color, float intensity);
};

class PointLight : public Light {
public:
    static std::unique_ptr<PointLight> create(TSVec3f position, TSVec3f color, float intensity,
                                               float constant = 1.0f, float linear = 0.09f, float quadratic = 0.032f,
                                               const std::string& name = "");
    std::unique_ptr<Light> clone() const override;

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

class AreaLight : public Light {
public:
    static std::unique_ptr<AreaLight> create(
        TSVec3f position,
        TSVec3f direction,
        TSVec3f color,
        float intensity,
        float width = 1.0f,
        float height = 1.0f,
        bool twoSided = false,
        const std::string& name = "");
    std::unique_ptr<Light> clone() const override;

    void setPosition(TSVec3f position);
    TSVec3f getPosition() const;
    void setWidth(float width);
    void setHeight(float height);
    void setSize(float width, float height);
    float getWidth() const;
    float getHeight() const;
    void setTwoSided(bool twoSided);
    bool isTwoSided() const;

private:
    AreaLight() = default;
    AreaLight(
        const std::string& name,
        TSVec3f position,
        TSVec3f direction,
        TSVec3f color,
        float intensity,
        float width,
        float height,
        bool twoSided);

    TSVec3f position_;
    float width_ = 1.0f;
    float height_ = 1.0f;
    bool twoSided_ = false;
};

class SpotLight : public Light {
public:
    static std::unique_ptr<SpotLight> create(TSVec3f position, TSVec3f direction, TSVec3f color,
                                               float intensity, float cutoff = 12.5f,
                                               const std::string& name = "");
    std::unique_ptr<Light> clone() const override;

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

} // namespace Tasrovy::Render
