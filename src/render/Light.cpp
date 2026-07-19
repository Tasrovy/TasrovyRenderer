#include "Light.h"
#include <algorithm>
namespace Tasrovy::Render {

Light::~Light() = default;

Light::Light(const std::string& name, TSVec3f direction, TSVec3f color, float intensity)
    : name_(name)
    , direction_(direction)
    , color_(color)
    , intensity_(intensity) {
}

void Light::setName(const std::string& name) { name_ = name; }
const std::string& Light::getName() const { return name_; }

void Light::setDirection(TSVec3f dir) { direction_ = dir; }
void Light::setColor(TSVec3f color) { color_ = color; }
void Light::setIntensity(float intensity) { intensity_ = intensity; }

TSVec3f Light::getDirection() const { return direction_; }
TSVec3f Light::getColor() const { return color_; }
float Light::getIntensity() const { return intensity_; }

// --- DirectionalLight ---

std::unique_ptr<DirectionalLight> DirectionalLight::create(TSVec3f direction, TSVec3f color, float intensity,
                                                           const std::string& name) {
    return std::unique_ptr<DirectionalLight>(new DirectionalLight(name, direction, color, intensity));
}

DirectionalLight::DirectionalLight(const std::string& name, TSVec3f direction, TSVec3f color, float intensity)
    : Light(name, direction, color, intensity) {
}

std::unique_ptr<Light> DirectionalLight::clone() const {
    return DirectionalLight::create(direction_, color_, intensity_, name_);
}

// --- PointLight ---

std::unique_ptr<PointLight> PointLight::create(TSVec3f position, TSVec3f color, float intensity,
                                                float constant, float linear, float quadratic,
                                                const std::string& name) {
    return std::unique_ptr<PointLight>(new PointLight(name, position, color, intensity, constant, linear, quadratic));
}

PointLight::PointLight(const std::string& name, TSVec3f position, TSVec3f color, float intensity,
                       float constant, float linear, float quadratic)
    : Light(name, TSVec3f(0.0f), color, intensity)
    , position_(position)
    , constant_(constant)
    , linear_(linear)
    , quadratic_(quadratic) {
}

std::unique_ptr<Light> PointLight::clone() const {
    return PointLight::create(position_, color_, intensity_, constant_, linear_, quadratic_, name_);
}

void PointLight::setPosition(TSVec3f pos) { position_ = pos; }
TSVec3f PointLight::getPosition() const { return position_; }

void PointLight::setConstant(float c) { constant_ = c; }
void PointLight::setLinear(float l) { linear_ = l; }
void PointLight::setQuadratic(float q) { quadratic_ = q; }
float PointLight::getConstant() const { return constant_; }
float PointLight::getLinear() const { return linear_; }
float PointLight::getQuadratic() const { return quadratic_; }

// --- AreaLight ---

std::unique_ptr<AreaLight> AreaLight::create(
    TSVec3f position,
    TSVec3f direction,
    TSVec3f color,
    float intensity,
    float width,
    float height,
    bool twoSided,
    const std::string& name) {
    return std::unique_ptr<AreaLight>(new AreaLight(
        name, position, direction, color, intensity, width, height, twoSided));
}

AreaLight::AreaLight(
    const std::string& name,
    TSVec3f position,
    TSVec3f direction,
    TSVec3f color,
    float intensity,
    float width,
    float height,
    bool twoSided)
    : Light(name, direction, color, intensity)
    , position_(position)
    , width_(std::max(width, 0.001f))
    , height_(std::max(height, 0.001f))
    , twoSided_(twoSided) {
}

std::unique_ptr<Light> AreaLight::clone() const {
    return AreaLight::create(
        position_, direction_, color_, intensity_, width_, height_, twoSided_, name_);
}

void AreaLight::setPosition(TSVec3f position) { position_ = position; }
TSVec3f AreaLight::getPosition() const { return position_; }
void AreaLight::setWidth(float width) { width_ = std::max(width, 0.001f); }
void AreaLight::setHeight(float height) { height_ = std::max(height, 0.001f); }
void AreaLight::setSize(float width, float height) {
    width_ = std::max(width, 0.001f);
    height_ = std::max(height, 0.001f);
}
float AreaLight::getWidth() const { return width_; }
float AreaLight::getHeight() const { return height_; }
void AreaLight::setTwoSided(bool twoSided) { twoSided_ = twoSided; }
bool AreaLight::isTwoSided() const { return twoSided_; }

// --- SpotLight ---

std::unique_ptr<SpotLight> SpotLight::create(TSVec3f position, TSVec3f direction, TSVec3f color,
                                              float intensity, float cutoff,
                                              const std::string& name) {
    return std::unique_ptr<SpotLight>(new SpotLight(name, position, direction, color, intensity, cutoff));
}

SpotLight::SpotLight(const std::string& name, TSVec3f position, TSVec3f direction, TSVec3f color,
                     float intensity, float cutoff)
    : Light(name, direction, color, intensity)
    , position_(position)
    , cutoff_(cutoff) {
}

std::unique_ptr<Light> SpotLight::clone() const {
    return SpotLight::create(position_, direction_, color_, intensity_, cutoff_, name_);
}

void SpotLight::setPosition(TSVec3f pos) { position_ = pos; }
void SpotLight::setCutoff(float degrees) { cutoff_ = degrees; }
TSVec3f SpotLight::getPosition() const { return position_; }
float SpotLight::getCutoff() const { return cutoff_; }

} // namespace Tasrovy::Render
