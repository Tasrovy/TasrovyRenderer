#include "StylizedPBRPipeline.h"

#include "DeferredPipeline.h"
#include "PipelinePass.h"
#include "Shader.h"

#include <utility>

namespace Tasrovy::Render {

std::shared_ptr<StylizedPBRPipeline> StylizedPBRPipeline::create(
    const std::string& name) {
    return std::shared_ptr<StylizedPBRPipeline>(
        new StylizedPBRPipeline(name));
}

StylizedPBRPipeline::StylizedPBRPipeline(const std::string& name)
    : PipelineBase(name) {
}

bool StylizedPBRPipeline::applyConfiguration(
    const PipelineConfiguration& configuration) {
    return PipelineBase::applyConfiguration(configuration);
}

void StylizedPBRPipeline::GenPass(std::shared_ptr<Scene> scene) {
    clearPasses();
    clearTextures();
    clearBuffers();

    // Phase 0 compatibility baseline. Keeping the destination pipeline as a
    // direct PipelineBase subclass lets each captured stage replace one of
    // these nodes without changing or inheriting DeferredPipeline itself.
    auto compatibility = DeferredPipeline::create("StylizedPBR.Compatibility");
    compatibility->applyConfiguration(configuration_);
    compatibility->GenPass(std::move(scene));

    declareTexture({
        "Stylized.TransmittanceLUT",
        PipelineTextureFormat::R11G11B10Float,
        PipelineTextureExtent::Fixed,
        1.0f,
        1.0f,
        256u,
        64u
    });
    declareTexture({
        "Stylized.MultiScatteringLUT",
        PipelineTextureFormat::R11G11B10Float,
        PipelineTextureExtent::Fixed,
        1.0f,
        1.0f,
        32u,
        32u
    });
    declareTexture({
        "Stylized.SkyViewLUT",
        PipelineTextureFormat::R11G11B10Float,
        PipelineTextureExtent::Fixed,
        1.0f,
        1.0f,
        128u,
        128u
    });

    for (const auto& texture : compatibility->getTextures()) {
        declareTexture(texture);
    }
    for (const auto& buffer : compatibility->getBuffers()) {
        declareBuffer(buffer);
    }

    auto transmittancePass = PipelinePass::create(
        "Stylized.Atmosphere.TransmittanceLUT");
    transmittancePass->setType(PipelinePassType::PostProcess);
    transmittancePass->setExecution(PipelinePassExecution::Fullscreen);
    transmittancePass->setTopology(Topology::TriangleList);
    transmittancePass->setCullMode(CullMode::None);
    transmittancePass->setDepthTest(false);
    transmittancePass->setDepthWrite(false);
    transmittancePass->setBlendMode(BlendMode::Off);
    transmittancePass->setVertexLayout({});
    transmittancePass->setVertexShader(Shader::create(
        "res/Shaders/Source/stylized_atmosphere_transmittance.hlsl",
        ShaderType::Vertex));
    transmittancePass->setFragmentShader(Shader::create(
        "res/Shaders/Source/stylized_atmosphere_transmittance.hlsl",
        ShaderType::Fragment));
    transmittancePass->addColorAttachment("Stylized.TransmittanceLUT");
    addPass(std::move(transmittancePass));

    auto multiScatteringPass = PipelinePass::create(
        "Stylized.Atmosphere.MultiScatteringLUT");
    multiScatteringPass->setType(PipelinePassType::PostProcess);
    multiScatteringPass->setExecution(PipelinePassExecution::Fullscreen);
    multiScatteringPass->setTopology(Topology::TriangleList);
    multiScatteringPass->setCullMode(CullMode::None);
    multiScatteringPass->setDepthTest(false);
    multiScatteringPass->setDepthWrite(false);
    multiScatteringPass->setBlendMode(BlendMode::Off);
    multiScatteringPass->setVertexLayout({});
    multiScatteringPass->setVertexShader(Shader::create(
        "res/Shaders/Source/stylized_atmosphere_multiscattering.hlsl",
        ShaderType::Vertex));
    multiScatteringPass->setFragmentShader(Shader::create(
        "res/Shaders/Source/stylized_atmosphere_multiscattering.hlsl",
        ShaderType::Fragment));
    multiScatteringPass->addSampledTexture(
        "TransmittanceLUT", "Stylized.TransmittanceLUT", 0u, false,
        "Stylized.Atmosphere.TransmittanceLUT");
    multiScatteringPass->addColorAttachment(
        "Stylized.MultiScatteringLUT");
    addPass(std::move(multiScatteringPass));

    auto skyViewPass = PipelinePass::create(
        "Stylized.Atmosphere.SkyViewLUT");
    skyViewPass->setType(PipelinePassType::PostProcess);
    skyViewPass->setExecution(PipelinePassExecution::Fullscreen);
    skyViewPass->setTopology(Topology::TriangleList);
    skyViewPass->setCullMode(CullMode::None);
    skyViewPass->setDepthTest(false);
    skyViewPass->setDepthWrite(false);
    skyViewPass->setBlendMode(BlendMode::Off);
    skyViewPass->setVertexLayout({});
    skyViewPass->setVertexShader(Shader::create(
        "res/Shaders/Source/stylized_atmosphere_skyview.hlsl",
        ShaderType::Vertex));
    skyViewPass->setFragmentShader(Shader::create(
        "res/Shaders/Source/stylized_atmosphere_skyview.hlsl",
        ShaderType::Fragment));
    skyViewPass->addSampledTexture(
        "TransmittanceLUT", "Stylized.TransmittanceLUT", 0u, false,
        "Stylized.Atmosphere.TransmittanceLUT");
    skyViewPass->addSampledTexture(
        "MultiScatteringLUT", "Stylized.MultiScatteringLUT", 1u, false,
        "Stylized.Atmosphere.MultiScatteringLUT");
    skyViewPass->addColorAttachment("Stylized.SkyViewLUT");
    addPass(std::move(skyViewPass));

    for (const auto& pass : compatibility->getPasses()) {
        addPass(pass);
    }
}

} // namespace Tasrovy::Render
