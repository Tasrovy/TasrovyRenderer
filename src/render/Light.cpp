#include "Light.h"
namespace Tasrovy {

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

void PointLight::setPosition(TSVec3f pos) { position_ = pos; }
TSVec3f PointLight::getPosition() const { return position_; }

void PointLight::setConstant(float c) { constant_ = c; }
void PointLight::setLinear(float l) { linear_ = l; }
void PointLight::setQuadratic(float q) { quadratic_ = q; }
float PointLight::getConstant() const { return constant_; }
float PointLight::getLinear() const { return linear_; }
float PointLight::getQuadratic() const { return quadratic_; }

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

void SpotLight::setPosition(TSVec3f pos) { position_ = pos; }
void SpotLight::setCutoff(float degrees) { cutoff_ = degrees; }
TSVec3f SpotLight::getPosition() const { return position_; }
float SpotLight::getCutoff() const { return cutoff_; }

} // namespace Tasrovy
