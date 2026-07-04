#include <volk.h>
#include <Dependencies.h>
#include <chrono>
#include <TSMatrix.h>
#include <Logger.hpp>
#include <Window.h>
#include <UI.h>
#include <imgui.h>
#include <Components.h>
// Render layer

#include "Scene.h"
#include "Camera.h"
#include "Light.h"
#include "Object.h"
#include "Skybox.h"

using namespace Tasrovy;

struct SkyUniformBufferObject {
    TSMat4f view;
    TSMat4f proj;
};

void updateUniformBuffer(
    VulkanBuffer& uniformBuffer, VulkanBuffer& skyUniformBuffer,
    VkExtent2D extent,
    const TSMat4f& modelMat, const TSMat4f& viewMat, const TSMat4f& projMat,
    TSVec3f camPos, TSVec3f lightDir, TSVec3f lightColor, float lightIntensity,
    float metallic, float roughness, float ao
) {
    UniformBufferObject ubo{};
    ubo.model = transpose(modelMat);
    ubo.view = transpose(viewMat);
    ubo.proj = transpose(projMat);
    ubo.camPos = TSVec4f(camPos, 1.0f);
    ubo.lightDir = TSVec4f(normalize(lightDir), 0.0f);
    ubo.lightColor = TSVec4f(lightColor, lightIntensity);
    ubo.metallic = metallic;
    ubo.roughness = roughness;
    ubo.ao = ao;
    uniformBuffer.setData(&ubo, sizeof(ubo));

    SkyUniformBufferObject subo{};
    subo.view = transpose(viewMat);
    subo.proj = transpose(projMat);
    skyUniformBuffer.setData(&subo, sizeof(subo));
}

int main()
{
    if (volkInitialize() != VK_SUCCESS) {
        LOG_CRITICAL("Failed to initialize volk!");
        return -1;
    }
    Tasrovy::Logger::Init();

    Window window(1280, 800, "TasrovyRenderer");

    // ======== RHI init ========
    auto extensions = window.getRequiredVulkanExtensions();
    VulkanContext context("Vulkan", extensions,
        [&window](VkInstance instance) { return window.createVulkanSurface(instance); },
        window.getWidth(), window.getHeight());
    context.updateFramebufferSize(window.getWidth(), window.getHeight());

    VulkanQueue graphicsQueue(context, QueueType::Graphics);
    VulkanQueue presentQueue(context, QueueType::Present);
    Renderer renderer(context, 4);
    ImmediateSubmitter immediateSubmitter(context, graphicsQueue);
    VulkanSwapchain swapchain(context);

    // ======== Scene setup ========
    auto scene = Scene::create("MainScene");

    auto camera = Camera::create(
        TSVec3f(0.0f, 1.0f, 5.0f), TSVec3f(0.0f),
        45.0f, (float)window.getWidth() / (float)window.getHeight(),
        0.1f, 100.0f, "MainCamera");
    scene->addCamera(std::move(camera));
    scene->setPrimaryCamera(scene->getCameras().back().get());

    auto dirLight = DirectionalLight::create(
        TSVec3f(-0.5f, -1.0f, -0.8f), TSVec3f(1.0f), 10.0f, "Sun");
    scene->addLight(std::move(dirLight));

    auto mainObj = Object::create("MainModel");
    scene->addObject(std::move(mainObj));

    scene->addObject(Skybox::create("MainSkybox"));

    // ======== Load model ========
    Model model("res\\model.obj");

    VulkanBuffer vertexBuffer(context, model.getVertices().size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VulkanBuffer indexBuffer(context, model.getIndices().size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    immediateSubmitter.copyDataToBuffer(model.getVertices().data(), vertexBuffer,
        model.getVertices().size() * sizeof(Vertex));
    immediateSubmitter.copyDataToBuffer(model.getIndices().data(), indexBuffer,
        model.getIndices().size() * sizeof(uint32_t));

    // ======== Load textures ========
    auto diffuseImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\diffuse.png", true);
    auto normalImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\normal.png", false, VK_FORMAT_R8G8B8A8_UNORM);
    auto emissiveImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\emissive.png", false);
    auto msaImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\msa.png", false);

    // ======== Load skybox cubemaps ========
    LOG_INFO("Before skybox cubemap");
    auto purpleSky = VulkanImage::createCubemapFromFile(context, immediateSubmitter, "res\\PurpleSky");
    LOG_INFO("After skybox cubemap");

    // ======== IBL ========
    LOG_INFO("Before IBL");
    IBLProcessor ibl(context, immediateSubmitter);
    ibl.addSkybox(*purpleSky, "PurpleSky");
    LOG_INFO("After IBL");

    // ======== Skybox vertex/index buffers ========
    LOG_INFO("Before skybox buffers");
    VulkanBuffer skyboxVB(context, skyboxVertices.size() * sizeof(SkyboxVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VulkanBuffer skyboxIB(context, skyboxIndices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    immediateSubmitter.copyDataToBuffer((void*)skyboxVertices.data(), skyboxVB,
        skyboxVertices.size() * sizeof(SkyboxVertex));
    immediateSubmitter.copyDataToBuffer((void*)skyboxIndices.data(), skyboxIB,
        skyboxIndices.size() * sizeof(uint32_t));
    LOG_INFO("After skybox buffers");

    // ======== Uniform buffers ========
    LOG_INFO("Before uniform buffers");
    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> skyUniformBuffers;
    for (int i = 0; i < renderer.getMaxFramesInFlight(); ++i) {
        uniformBuffers.push_back(std::make_unique<VulkanBuffer>(context, sizeof(UniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
        skyUniformBuffers.push_back(std::make_unique<VulkanBuffer>(context, sizeof(SkyUniformBufferObject),
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    }
    LOG_INFO("After uniform buffers");

    // ======== PBR pipeline ========
    LOG_INFO("Before PBR pipeline");
    PipelineBuilder pipelineBuilder(context);
    pipelineBuilder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "res\\vert.spv", "VSMain");
    pipelineBuilder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "res\\frag.spv", "PSMain");

    VkVertexInputBindingDescription pbrBinding{};
    pbrBinding.binding = 0;
    pbrBinding.stride = sizeof(Vertex);
    pbrBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 5> pbrAttrs{};
    pbrAttrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) };
    pbrAttrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal) };
    pbrAttrs[2] = { 2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, tangent) };
    pbrAttrs[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, bitangent) };
    pbrAttrs[4] = { 4, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, texCoord) };

    VkPipelineVertexInputStateCreateInfo pbrVertexInput{};
    pbrVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    pbrVertexInput.vertexBindingDescriptionCount = 1;
    pbrVertexInput.pVertexBindingDescriptions = &pbrBinding;
    pbrVertexInput.vertexAttributeDescriptionCount = 5;
    pbrVertexInput.pVertexAttributeDescriptions = pbrAttrs.data();
    pipelineBuilder.setVertexInputState(pbrVertexInput);

    VkPipelineInputAssemblyStateCreateInfo pbrAssembly{};
    pbrAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    pbrAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    pbrAssembly.primitiveRestartEnable = VK_FALSE;
    pipelineBuilder.setInputAssemblyState(pbrAssembly);
    pipelineBuilder.setRenderingFormats(swapchain.getImageFormat(), context.findDepthFormat());

    VulkanDescriptorSetLayout::Builder pbrLayoutBuilder(context);
    pbrLayoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    for (uint32_t i = 1; i <= 4; ++i)
        pbrLayoutBuilder.addBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    pbrLayoutBuilder.addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    pbrLayoutBuilder.addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    pbrLayoutBuilder.addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto pbrDescriptorSetLayout = pbrLayoutBuilder.build();

    VulkanDescriptorPool::Builder pbrPoolBuilder(context);
    pbrPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, renderer.getMaxFramesInFlight());
    pbrPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * renderer.getMaxFramesInFlight());
    pbrPoolBuilder.setMaxSets(renderer.getMaxFramesInFlight());
    pbrPoolBuilder.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    auto pbrDescriptorPool = pbrPoolBuilder.build();

    std::vector<VkDescriptorSet> pbrDescriptorSets(renderer.getMaxFramesInFlight());
    for (int i = 0; i < renderer.getMaxFramesInFlight(); ++i)
        pbrDescriptorSets[i] = pbrDescriptorPool->allocateSet(*pbrDescriptorSetLayout);

    pipelineBuilder.addDescriptorSetLayout(pbrDescriptorSetLayout->getLayout());
    auto pbrPipeline = pipelineBuilder.buildGraphicsPipeline();
    LOG_INFO("After PBR pipeline");

    // ======== Skybox pipeline ========
    LOG_INFO("Before sky pipeline");
    PipelineBuilder skyPipelineBuilder(context);
    skyPipelineBuilder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "res\\skyvert.spv", "VSMain");
    skyPipelineBuilder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "res\\skyfrag.spv", "PSMain");

    auto skyBindings = SkyboxVertex::getBindingDescriptions();
    auto skyAttribs = SkyboxVertex::getAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo skyVertexInput{};
    skyVertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    skyVertexInput.vertexBindingDescriptionCount = 1;
    skyVertexInput.pVertexBindingDescriptions = skyBindings.data();
    skyVertexInput.vertexAttributeDescriptionCount = 1;
    skyVertexInput.pVertexAttributeDescriptions = skyAttribs.data();
    skyPipelineBuilder.setVertexInputState(skyVertexInput);

    VkPipelineInputAssemblyStateCreateInfo skyAssembly{};
    skyAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    skyAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    skyAssembly.primitiveRestartEnable = VK_FALSE;
    skyPipelineBuilder.setInputAssemblyState(skyAssembly);
    skyPipelineBuilder.setRenderingFormats(swapchain.getImageFormat(), context.findDepthFormat());

    VkPipelineDepthStencilStateCreateInfo skyDepth{};
    skyDepth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    skyDepth.depthTestEnable = VK_TRUE;
    skyDepth.depthWriteEnable = VK_FALSE;
    skyDepth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyDepth.stencilTestEnable = VK_FALSE;
    skyPipelineBuilder.setDepthStencilState(skyDepth);

    VkPipelineRasterizationStateCreateInfo skyRaster{};
    skyRaster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    skyRaster.polygonMode = VK_POLYGON_MODE_FILL;
    skyRaster.lineWidth = 1.0f;
    skyRaster.cullMode = VK_CULL_MODE_FRONT_BIT;
    skyRaster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    skyPipelineBuilder.setRasterizationState(skyRaster);

    VulkanDescriptorSetLayout::Builder skyLayoutBuilder(context);
    skyLayoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    skyLayoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto skyDescriptorSetLayout = skyLayoutBuilder.build();

    VulkanDescriptorPool::Builder skyPoolBuilder(context);
    skyPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, renderer.getMaxFramesInFlight());
    skyPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, renderer.getMaxFramesInFlight());
    skyPoolBuilder.setMaxSets(renderer.getMaxFramesInFlight());
    skyPoolBuilder.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    auto skyDescriptorPool = skyPoolBuilder.build();

    std::vector<VkDescriptorSet> skyDescriptorSets(renderer.getMaxFramesInFlight());
    for (int i = 0; i < renderer.getMaxFramesInFlight(); ++i)
        skyDescriptorSets[i] = skyDescriptorPool->allocateSet(*skyDescriptorSetLayout);

    skyPipelineBuilder.addDescriptorSetLayout(skyDescriptorSetLayout->getLayout());
    auto skyPipeline = skyPipelineBuilder.buildGraphicsPipeline();
    LOG_INFO("After sky pipeline");

    // ======== Material params (editable via UI) ========
    float metallic = 0.5f, roughness = 0.5f, ao = 1.0f;

    // ======== ImGui ========
    LOG_INFO("Before UIOverlay");
    UIOverlay::CreateInfo uiInfo{};
    uiInfo.window = window.getHandle();
    uiInfo.instance = context.getInstance();
    uiInfo.physicalDevice = context.getPhysicalDevice();
    uiInfo.device = context.getDevice();
    uiInfo.graphicsQueue = graphicsQueue.getQueue();
    uiInfo.msaaSamples = context.getMsaaSamples();
    uiInfo.colorFormat = swapchain.getImageFormat();
    UIOverlay ui(uiInfo);
    LOG_INFO("After UIOverlay");

    ui.setDrawCallback([&]() {
        auto* cam = scene->getPrimaryCamera();
        auto* lgt = dynamic_cast<DirectionalLight*>(scene->getLights()[0].get());

        UI::Begin("Scene Controls");
        UI::Text("Frame Time: %.3f ms (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);

        if (cam && UI::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            TSVec3f p = cam->getPosition();
            TSVec3f r = cam->getRotationEuler();
            float fov = cam->getFOV();
            if (UI::DragFloat3("Position", &p.x, 0.1f)) cam->setPosition(p);
            if (UI::DragFloat3("Rotation", &r.x, 1.0f)) cam->setRotation(r);
            if (UI::SliderFloat("FOV", &fov, 10.0f, 120.0f)) cam->setFOV(fov);
        }

        if (lgt && UI::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            TSVec3f d = lgt->getDirection();
            TSVec3f c = lgt->getColor();
            float i = lgt->getIntensity();
            if (UI::DragFloat3("Direction", &d.x, 0.01f)) lgt->setDirection(d);
            if (UI::ColorEdit3("Color", &c.x)) lgt->setColor(c);
            if (UI::DragFloat("Intensity", &i, 0.1f, 0.0f, 100.0f)) lgt->setIntensity(i);
        }

        if (UI::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
            UI::SliderFloat("Metallic", &metallic, 0.0f, 1.0f);
            UI::SliderFloat("Roughness", &roughness, 0.0f, 1.0f);
            UI::SliderFloat("AO", &ao, 0.0f, 1.0f);
        }

        if (UI::CollapsingHeader("Scene Objects")) {
            for (auto& obj : scene->getObjects()) {
                if (UI::TreeNode(obj->getName().c_str())) {
                    auto& xf = obj->getTransform();
                    TSVec3f p = xf.getPosition();
                    TSVec3f r = xf.getRotationEuler();
                    TSVec3f s = xf.getScale();
                    if (UI::DragFloat3("Position", &p.x, 0.01f)) xf.setPosition(p);
                    if (UI::DragFloat3("Rotation", &r.x, 1.0f)) xf.setRotation(r);
                    if (UI::DragFloat3("Scale", &s.x, 0.01f)) xf.setScale(s);
                    UI::TreePop();
                }
            }
        }

        UI::End();
    });

    LOG_INFO("Setup complete, entering main loop");

    // ======== Main loop ========
    while (!window.shouldClose()) {
        window.pollEvents();

        if (window.wasResized()) {
            window.resetResizedFlag();
            while (window.getWidth() == 0 || window.getHeight() == 0) {
                window.waitEvents(); window.pollEvents();
            }
            context.updateFramebufferSize(window.getWidth(), window.getHeight());
            context.framebufferResized = true;
            if (auto* c = scene->getPrimaryCamera())
                c->setAspect((float)window.getWidth() / (float)window.getHeight());
        }
        context.CheckFormatChange(swapchain);

        // --- Read scene state ---
        auto* cam = scene->getPrimaryCamera();
        auto* lgt = dynamic_cast<DirectionalLight*>(scene->getLights()[0].get());

        TSVec3f camPos = cam->getPosition();

        // --- Compute matrices ---
        TSMat4f modelMat = TSMat4f(1.0f);
        TSMat4f viewMat = inverse(cam->getViewMatrix());
        TSMat4f projMat = cam->getProjectionMatrix();
        projMat[1][1] *= -1;

        // --- Update UBOs ---
        uint32_t frame = renderer.getCurrentFrame();
        updateUniformBuffer(
            *uniformBuffers[frame], *skyUniformBuffers[frame],
            swapchain.getExtent(), modelMat, viewMat, projMat,
            camPos, lgt->getDirection(), lgt->getColor(), lgt->getIntensity(),
            metallic, roughness, ao
        );

        // --- Write PBR descriptors ---
        VkDescriptorBufferInfo pbrBufInfo = uniformBuffers[frame]->getDescriptorInfo();
        VkDescriptorImageInfo diffuseInfo = diffuseImage->getDescriptorInfo();
        VkDescriptorImageInfo normalInfo = normalImage->getDescriptorInfo();
        VkDescriptorImageInfo emissiveInfo = emissiveImage->getDescriptorInfo();
        VkDescriptorImageInfo msaInfo = msaImage->getDescriptorInfo();
        VkDescriptorImageInfo irradianceInfo = ibl.getIrradianceMap("PurpleSky")->getDescriptorInfo();
        VkDescriptorImageInfo prefilteredInfo = ibl.getPrefilteredMap("PurpleSky")->getDescriptorInfo();
        VkDescriptorImageInfo brdfInfo = ibl.getBrdfLUT()->getDescriptorInfo();

        DescriptorWriter(context, pbrDescriptorSets[frame])
            .writeBuffer(0, &pbrBufInfo)
            .writeImage(1, &diffuseInfo)
            .writeImage(2, &normalInfo)
            .writeImage(3, &emissiveInfo)
            .writeImage(4, &msaInfo)
            .writeImage(5, &irradianceInfo)
            .writeImage(6, &prefilteredInfo)
            .writeImage(7, &brdfInfo)
            .update();

        // --- Write sky descriptors ---
        VkDescriptorBufferInfo skyBufInfo = skyUniformBuffers[frame]->getDescriptorInfo();
        VkDescriptorImageInfo skyImgInfo = purpleSky->getDescriptorInfo();

        DescriptorWriter(context, skyDescriptorSets[frame])
            .writeBuffer(0, &skyBufInfo)
            .writeImage(1, &skyImgInfo)
            .update();

        // --- Render ---
        ui.beginFrame();

        VkCommandBuffer cmd = renderer.beginFrame(swapchain);
        if (!cmd) continue;

        renderer.beginRenderPass(cmd, swapchain);

        VkViewport vp{};
        vp.width = static_cast<float>(swapchain.getExtent().width);
        vp.height = static_cast<float>(swapchain.getExtent().height);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D sc{};
        sc.extent = swapchain.getExtent();
        vkCmdSetScissor(cmd, 0, 1, &sc);

        // Draw skybox
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline->getPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            skyPipeline->getLayout(), 0, 1, &skyDescriptorSets[frame], 0, nullptr);
        VkBuffer skyVbs[] = { skyboxVB.getBuffer() };
        VkDeviceSize skyOff[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, skyVbs, skyOff);
        vkCmdBindIndexBuffer(cmd, skyboxIB.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, (uint32_t)skyboxIndices.size(), 1, 0, 0, 0);

        // Draw model
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pbrPipeline->getPipeline());
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            pbrPipeline->getLayout(), 0, 1, &pbrDescriptorSets[frame], 0, nullptr);
        VkBuffer modelVbs[] = { vertexBuffer.getBuffer() };
        VkDeviceSize zero[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, modelVbs, zero);
        vkCmdBindIndexBuffer(cmd, indexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, (uint32_t)model.getIndices().size(), 1, 0, 0, 0);

        renderer.endRenderPass(cmd, swapchain);

        // Draw ImGui on top of scene (dynamic rendering)
        ui.endFrame(cmd, swapchain.getColorAttachmentView(), swapchain.getExtent());

        renderer.endFrame(swapchain, graphicsQueue, presentQueue);
    }

    vkDeviceWaitIdle(context.getDevice());
}
