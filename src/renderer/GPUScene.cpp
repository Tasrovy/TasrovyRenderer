#include "GPUScene.h"

#include "ViewState.h"
#include "ViewSystem.h"
#include "../RHI/Buffer.h"
#include "../render/Camera.h"
#include "../render/FrameCompiler.h"
#include "../render/Material.h"
#include "../render/Object.h"
#include "../render/Scene.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace Tasrovy::Renderer {
namespace {

using namespace Tasrovy::Base;
using namespace Tasrovy::Render;

uint32_t requiredCapacity(
    const std::unordered_map<uint64_t, uint32_t>& indices) {
    uint32_t result = 1;
    for (const auto& [_, index] : indices) result = std::max(result, index + 1);
    return result;
}

uint32_t requiredObjectCapacity(const FrameSourceRegistry& sources) {
    uint32_t result = 1;
    for (const auto& [index, _] : sources.objectDataSources) {
        result = std::max(result, index + 1);
    }
    return result;
}

MaterialTextureUvSampling textureSampling(
    const Material& material,
    const char* slot) {
    const auto* binding = material.getTextureBinding(slot);
    return binding ? binding->uvSampling : MaterialTextureUvSampling{};
}

TSVec4f packedUvTransform(const MaterialTextureUvSampling& sampling) {
    return TSVec4f(
        sampling.scale.x, sampling.scale.y,
        sampling.offset.x, sampling.offset.y);
}

template <typename T>
const std::shared_ptr<T>& checkedFrame(
    const std::vector<std::shared_ptr<T>>& values, uint32_t frame) {
    if (frame >= values.size()) throw std::out_of_range("GPUScene frame index");
    return values[frame];
}

} // namespace

void GPUScene::reset() {
    objectCapacity_ = materialCapacity_ = 0;
    viewBuffers_.clear();
    objectBuffers_.clear();
    materialBuffers_.clear();
    lightBuffers_.clear();
}

void GPUScene::prepare(
    RHI::Device& device,
    RHI::Device::ResourceScope scope,
    uint32_t framesInFlight,
    const FrameSourceRegistry& sources) {
    reset();
    const uint32_t frameCount = std::max(framesInFlight, 1u);
    objectCapacity_ = requiredObjectCapacity(sources);
    materialCapacity_ = requiredCapacity(sources.materialIndices);
    viewBuffers_.resize(frameCount);
    objectBuffers_.resize(frameCount);
    materialBuffers_.resize(frameCount);
    lightBuffers_.resize(frameCount);
    for (uint32_t frame = 0; frame < frameCount; ++frame) {
        viewBuffers_[frame] = device.retainResource(
            scope, device.createUniformBuffer(sizeof(ViewUniform)));
        objectBuffers_[frame] = device.retainResource(scope, device.createBuffer({
            sizeof(ObjectData) * objectCapacity_, RHI::BufferUsage::Storage, true}));
        materialBuffers_[frame] = device.retainResource(scope, device.createBuffer({
            sizeof(MaterialData) * materialCapacity_, RHI::BufferUsage::Storage, true}));
        lightBuffers_[frame] = device.retainResource(scope, device.createBuffer({
            sizeof(SceneLightData), RHI::BufferUsage::Storage, true}));
    }
}

void GPUScene::buildUploads(
    uint32_t frameIndex,
    const Scene& scene,
    const Camera& camera,
    const ViewFrameData& viewFrame,
    const ViewState& viewState,
    const FrameSourceRegistry& sources,
    const RendererSettings& settings,
    bool environmentLightingEnabled,
    uint32_t internalWidth,
    uint32_t internalHeight,
    uint32_t displayWidth,
    uint32_t displayHeight,
    std::vector<FrameBufferUpload>& uploads) const {
    const auto appendUpload = [&uploads](
        const std::shared_ptr<RHI::Buffer>& buffer,
        const void* data,
        size_t byteSize) {
        FrameBufferUpload upload;
        upload.buffer = buffer;
        upload.bytes.resize(byteSize);
        if (byteSize != 0) {
            std::memcpy(upload.bytes.data(), data, byteSize);
        }
        uploads.push_back(std::move(upload));
    };
    const auto resolution = FrameParameterBuilder::buildResolution(
        internalWidth, internalHeight, displayWidth, displayHeight,
        settings.temporalAAMode);
    ViewUniform view{};
    view.view = transpose(viewFrame.view);
    view.projection = transpose(viewFrame.flippedProjection);
    view.unflippedProjection = transpose(viewFrame.unflippedProjection);
    view.previousView = transpose(
        viewState.temporalHistoryValid ? viewState.previousView : viewFrame.view);
    view.previousProjection = transpose(viewState.temporalHistoryValid
        ? viewState.previousFlippedProjection : viewFrame.flippedProjection);
    view.previousUnflippedProjection = transpose(viewState.temporalHistoryValid
        ? viewState.previousUnflippedProjection : viewFrame.unflippedProjection);
    view.cameraPositionAndNear = TSVec4f(camera.getPosition(), camera.getNearPlane());
    view.renderSizeAndFar = TSVec4f(
        static_cast<float>(internalWidth), static_cast<float>(internalHeight),
        static_cast<float>(displayWidth), camera.getFarPlane());
    view.jitterAndMipBias = TSVec4f(
        viewFrame.jitterUv.x, viewFrame.jitterUv.y,
        resolution.temporalMipBias, 0.0f);
    appendUpload(
        checkedFrame(viewBuffers_, frameIndex), &view, sizeof(view));

    std::vector<MaterialData> materials(materialCapacity_);
    for (const auto& [materialId, material] : sources.materials) {
        const auto indexIt = sources.materialIndices.find(materialId);
        if (!material || indexIt == sources.materialIndices.end() ||
            indexIt->second >= materials.size()) continue;
        auto& data = materials[indexIt->second];
        const auto baseColor = material->getVec4("baseColorFactor", TSVec4f(1.0f));
        data.baseColorFactorAndTexture = TSVec4f(
            baseColor.x, baseColor.y, baseColor.z,
            material->hasTexture("baseColorTexture") ? 1.0f : 0.0f);
        data.surface = TSVec4f(
            material->getFloat("metallic", 0.0f),
            material->getFloat("roughness", 1.0f),
            material->getFloat("ao", 1.0f), 0.0f);
        data.emission.x = material->getFloat("emissiveIntensity", 0.0f);
        data.rimColorAndStrength = TSVec4f(
            material->getVec3("rimColor", TSVec3f(1.0f)),
            material->getFloat("rimStrength", 0.0f));
        data.rimParams.x = material->getFloat("rimPower", 3.0f);
        const auto baseColorUv = textureSampling(*material, "baseColorTexture");
        const auto normalUv = textureSampling(*material, "normalTexture");
        const auto emissiveUv = textureSampling(*material, "emissiveTexture");
        const auto mraUv = textureSampling(
            *material, "metallicRoughnessAOTexture");
        data.baseColorUvTransform = packedUvTransform(baseColorUv);
        data.normalUvTransform = packedUvTransform(normalUv);
        data.emissiveUvTransform = packedUvTransform(emissiveUv);
        data.mraUvTransform = packedUvTransform(mraUv);
        data.textureUvModes = TSVec4f(
            static_cast<float>(baseColorUv.mode),
            static_cast<float>(normalUv.mode),
            static_cast<float>(emissiveUv.mode),
            static_cast<float>(mraUv.mode));
    }
    appendUpload(
        checkedFrame(materialBuffers_, frameIndex),
        materials.data(), materials.size() * sizeof(MaterialData));

    std::vector<ObjectData> objects(objectCapacity_);
    for (const auto& [objectIndex, source] : sources.objectDataSources) {
        const auto object = source.object.lock();
        if (!object || objectIndex >= objects.size()) continue;
        auto& data = objects[objectIndex];
        const auto model = object->getModelMatrix();
        data.model = transpose(model);
        const auto previous = viewState.previousModelMatrices.find(object.get());
        data.previousModel = transpose(
            viewState.temporalHistoryValid &&
                previous != viewState.previousModelMatrices.end()
                ? previous->second : model);
        const auto material = source.material
            ? source.material : object->getMaterial();
        if (material) {
            for (const auto& [materialId, candidate] : sources.materials) {
                if (candidate == material) {
                    if (const auto found = sources.materialIndices.find(materialId);
                        found != sources.materialIndices.end()) {
                        data.materialIndex = found->second;
                    }
                    break;
                }
            }
        }
        data.flags = object->getFlipProjectionY() ? 1u : 0u;
    }
    appendUpload(
        checkedFrame(objectBuffers_, frameIndex),
        objects.data(), objects.size() * sizeof(ObjectData));

    const auto lighting = FrameParameterBuilder::buildLighting(scene);
    SceneLightData lights{};
    lights.meta = TSVec4f(
        static_cast<float>(lighting.gpuLightCount),
        environmentLightingEnabled ? 1.0f : 0.0f,
        static_cast<float>(lighting.shadowLightIndex), 0.0f);
    lights.primaryDirection = TSVec4f(lighting.primaryDirection, 0.0f);
    lights.primaryColor = TSVec4f(
        lighting.primaryColor, lighting.primaryIntensity);
    lights.lights = lighting.gpuLights;
    appendUpload(
        checkedFrame(lightBuffers_, frameIndex), &lights, sizeof(lights));
}

const std::shared_ptr<RHI::Buffer>& GPUScene::viewBuffer(uint32_t frame) const {
    return checkedFrame(viewBuffers_, frame);
}
const std::shared_ptr<RHI::Buffer>& GPUScene::objectBuffer(uint32_t frame) const {
    return checkedFrame(objectBuffers_, frame);
}
const std::shared_ptr<RHI::Buffer>& GPUScene::materialBuffer(uint32_t frame) const {
    return checkedFrame(materialBuffers_, frame);
}
const std::shared_ptr<RHI::Buffer>& GPUScene::lightBuffer(uint32_t frame) const {
    return checkedFrame(lightBuffers_, frame);
}

} // namespace Tasrovy::Renderer
