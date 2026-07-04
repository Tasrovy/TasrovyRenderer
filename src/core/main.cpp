#include <volk.h>
#include <Dependencies.h>
#include <chrono>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/ext/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/string_cast.hpp>
#include <Logger.hpp>
#include <Window.h>
#include <UI.h>
#include <imgui.h>

struct Transform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f);
    glm::vec3 scale = glm::vec3(1.0f);
};

struct LightParams {
    glm::vec3 direction = glm::vec3(-0.5f, -1.0f, -0.8f);
    glm::vec3 color = glm::vec3(1.0f, 1.0f, 1.0f);
    float     intensity = 10.0f;
};

struct SkyUniformBufferObject {
    glm::mat4 view;
    glm::mat4 proj;
};

void updateUniformBuffer(
    VulkanBuffer& uniformBuffer,
    VulkanBuffer& skyUniformBuffer,
    VkExtent2D extent,
    const Transform& modelTransform,
    const Transform& cameraTransform,
    const LightParams& lightParams,
    float metallic,
    float roughness,
    float ao
) {
    // --- 1. ---

    // Model Matrix
    glm::mat4 modelMat = glm::mat4(1.0f);
    modelMat = glm::translate(modelMat, modelTransform.position);
    modelMat = glm::rotate(modelMat, glm::radians(modelTransform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    modelMat = glm::rotate(modelMat, glm::radians(modelTransform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    modelMat = glm::rotate(modelMat, glm::radians(modelTransform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    modelMat = glm::scale(modelMat, modelTransform.scale);

    // View Matrix
    glm::mat4 camWorldMat = glm::mat4(1.0f);
    camWorldMat = glm::translate(camWorldMat, cameraTransform.position);
    camWorldMat = glm::rotate(camWorldMat, glm::radians(cameraTransform.rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
    camWorldMat = glm::rotate(camWorldMat, glm::radians(cameraTransform.rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
    camWorldMat = glm::rotate(camWorldMat, glm::radians(cameraTransform.rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
    glm::mat4 viewMat = glm::inverse(camWorldMat);

    // Projection Matrix
    glm::mat4 projMat = glm::perspective(glm::radians(45.0f), (float)extent.width / (float)extent.height, 0.1f, 100.0f);
    projMat[1][1] *= -1;

    // --- 2. ---
    UniformBufferObject ubo{};
    ubo.model = glm::transpose(modelMat);
    ubo.view = glm::transpose(viewMat);
    ubo.proj = glm::transpose(projMat);
    ubo.camPos = glm::vec4(cameraTransform.position, 1.0f);
    ubo.lightDir = glm::vec4(glm::normalize(lightParams.direction), 0.0f);
    ubo.lightColor = glm::vec4(lightParams.color, lightParams.intensity);
    ubo.metallic = metallic;
    ubo.roughness = roughness;
    ubo.ao = ao;
    uniformBuffer.setData(&ubo, sizeof(ubo));

    // --- 3. ---
    SkyUniformBufferObject subo{};
    glm::mat4 skyViewMat = glm::mat4(glm::mat3(viewMat));
    subo.view = glm::transpose(skyViewMat);
    subo.proj = glm::transpose(projMat);
    skyUniformBuffer.setData(&subo, sizeof(subo));
}

int main()
{
    if (volkInitialize() != VK_SUCCESS) {
        LOG_CRITICAL("Failed to initialize volk! Your GPU driver may not support Vulkan.");
        return -1;
    }
    Tasrovy::Logger::Init();
    Transform modelTransform;
    Transform cameraTransform;
    cameraTransform.position = glm::vec3(0.0f, 1.0f, 5.0f);
    LightParams lightParams;
    Window window(1280, 800, "Vulkan");

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

    Model model("res\\model.obj");
    std::unique_ptr<VulkanImage> diffuseImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\diffuse.png", true);
    std::unique_ptr<VulkanImage> normalImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\normal.png", false, VK_FORMAT_R8G8B8A8_UNORM);
    std::unique_ptr<VulkanImage> emissiveImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\emissive.png", false);
    std::unique_ptr<VulkanImage> msaImage = VulkanImage::createTexture(context, immediateSubmitter, "res\\msa.png", false);

    std::map<std::string, std::unique_ptr<VulkanImage>> skyboxCubemaps;
    skyboxCubemaps["Purple Sky"] = VulkanImage::createCubemapFromFile(context, immediateSubmitter, "res\\PurpleSky");
    skyboxCubemaps["Blue Sky"] = VulkanImage::createCubemapFromFile(context, immediateSubmitter, "res\\BuleSky");
    skyboxCubemaps["Building Sky"] = VulkanImage::createCubemapFromFile(context, immediateSubmitter, "res\\BuildSky");
    skyboxCubemaps["Red Sky"] = VulkanImage::createCubemapFromFile(context, immediateSubmitter, "res\\RedSky");
    VulkanImage* currentSkybox = skyboxCubemaps.begin()->second.get();
    std::string currentSkyboxName = skyboxCubemaps.begin()->first;
    LOG_INFO("All SkyBoxes Built!");
    LOG_INFO("SkyBox Build!");

    IBLProcessor ibl = IBLProcessor(context, immediateSubmitter);
    ibl.addSkybox(*skyboxCubemaps["Purple Sky"], "Purple Sky");
    ibl.addSkybox(*skyboxCubemaps["Blue Sky"], "Blue Sky");
    ibl.addSkybox(*skyboxCubemaps["Building Sky"], "Building Sky");
    ibl.addSkybox(*skyboxCubemaps["Red Sky"], "Red Sky");

    VulkanBuffer vertexBuffer(
        context, model.getVertices().size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VulkanBuffer indexBuffer(context, model.getIndices().size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    immediateSubmitter.copyDataToBuffer(model.getVertices().data(), vertexBuffer, model.getVertices().size() * sizeof(Vertex));
    immediateSubmitter.copyDataToBuffer(model.getIndices().data(), indexBuffer, model.getIndices().size() * sizeof(uint32_t));

    VulkanBuffer skyboxVertexBuffer(
        context, skyboxVertices.size() * sizeof(SkyboxVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    VulkanBuffer skyboxIndexBuffer(
        context, skyboxIndices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    immediateSubmitter.copyDataToBuffer((void*)skyboxVertices.data(), skyboxVertexBuffer, skyboxVertices.size() * sizeof(SkyboxVertex));
    immediateSubmitter.copyDataToBuffer((void*)skyboxIndices.data(), skyboxIndexBuffer, skyboxIndices.size() * sizeof(uint32_t));

    std::vector<std::unique_ptr<VulkanBuffer>> uniformBuffers;
    std::vector<std::unique_ptr<VulkanBuffer>> skyUniformBuffers;
    for (int i = 0; i < renderer.getMaxFramesInFlight(); ++i) {
        uniformBuffers.push_back(
            std::make_unique<VulkanBuffer>(
                context, sizeof(UniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            )
        );
        skyUniformBuffers.push_back(
            std::make_unique<VulkanBuffer>(
                context, sizeof(SkyUniformBufferObject),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
            )
        );
    }

    PipelineBuilder pipelineBuilder(context);
    pipelineBuilder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "res\\vert.spv", "VSMain");
    pipelineBuilder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "res\\frag.spv", "PSMain");
    pipelineBuilder.setVertexInputState({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = (uint32_t)model.getVertexBindingDescription().size(),
        .pVertexBindingDescriptions = model.getVertexBindingDescription().data(),
        .vertexAttributeDescriptionCount = (uint32_t)model.getVertexAttributeDescription().size(),
        .pVertexAttributeDescriptions = model.getVertexAttributeDescription().data(),
        });
    pipelineBuilder.setInputAssemblyState({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
        });
    pipelineBuilder.setRenderingFormats(swapchain.getImageFormat(), context.findDepthFormat());

    VulkanDescriptorSetLayout::Builder descriptorSetLayoutBuilder(context);
    descriptorSetLayoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(2, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(3, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(4, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(5, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(6, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    descriptorSetLayoutBuilder.addBinding(7, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);

    std::unique_ptr<VulkanDescriptorSetLayout> descriptorSetLayout = descriptorSetLayoutBuilder.build();

    VulkanDescriptorPool::Builder descriptorPoolBuilder(context);
    descriptorPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, renderer.getMaxFramesInFlight());
    descriptorPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 7 * renderer.getMaxFramesInFlight());
    descriptorPoolBuilder.setMaxSets(renderer.getMaxFramesInFlight());
    descriptorPoolBuilder.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    std::unique_ptr<VulkanDescriptorPool> descriptorPool = descriptorPoolBuilder.build();

    std::vector<VkDescriptorSet> descriptorSets(renderer.getMaxFramesInFlight());
    for (int i = 0; i < renderer.getMaxFramesInFlight(); ++i) {
        descriptorSets[i] = descriptorPool->allocateSet(*descriptorSetLayout);
    }

    pipelineBuilder.addDescriptorSetLayout(descriptorSetLayout->getLayout());

    std::unique_ptr<VulkanPipeline> graphicsPipeline = pipelineBuilder.buildGraphicsPipeline();

    PipelineBuilder skyPipelineBuilder(context);
    skyPipelineBuilder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "res\\skyvert.spv", "VSMain");
    skyPipelineBuilder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "res\\skyfrag.spv", "PSMain");
    skyPipelineBuilder.setVertexInputState({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = 1,
        .pVertexBindingDescriptions = SkyboxVertex::getBindingDescriptions().data(),
        .vertexAttributeDescriptionCount = 1,
        .pVertexAttributeDescriptions = SkyboxVertex::getAttributeDescriptions().data(),
        });
    skyPipelineBuilder.setInputAssemblyState({
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        .primitiveRestartEnable = VK_FALSE,
        });
    skyPipelineBuilder.setRenderingFormats(swapchain.getImageFormat(), context.findDepthFormat());

    VkPipelineDepthStencilStateCreateInfo skyDepthStencil{};
    skyDepthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    skyDepthStencil.depthTestEnable = VK_TRUE;
    skyDepthStencil.depthWriteEnable = VK_FALSE;
    skyDepthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    skyDepthStencil.stencilTestEnable = VK_FALSE;
    skyPipelineBuilder.setDepthStencilState(skyDepthStencil);

    VkPipelineRasterizationStateCreateInfo skyRasterizer{};
    skyRasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    skyRasterizer.depthClampEnable = VK_FALSE;
    skyRasterizer.rasterizerDiscardEnable = VK_FALSE;
    skyRasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    skyRasterizer.lineWidth = 1.0f;
    skyRasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    skyRasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    skyRasterizer.depthBiasEnable = VK_FALSE;
    skyPipelineBuilder.setRasterizationState(skyRasterizer);

    VulkanDescriptorSetLayout::Builder skyDescriptorSetLayoutBuilder(context);
    skyDescriptorSetLayoutBuilder.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT);
    skyDescriptorSetLayoutBuilder.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    std::unique_ptr<VulkanDescriptorSetLayout> skyDescriptorSetLayout = skyDescriptorSetLayoutBuilder.build();

    VulkanDescriptorPool::Builder skyDescriptorPoolBuilder(context);
    skyDescriptorPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, renderer.getMaxFramesInFlight());
    skyDescriptorPoolBuilder.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, renderer.getMaxFramesInFlight());
    skyDescriptorPoolBuilder.setMaxSets(renderer.getMaxFramesInFlight());
    skyDescriptorPoolBuilder.setPoolFlags(VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
    std::unique_ptr<VulkanDescriptorPool> skyDescriptorPool = skyDescriptorPoolBuilder.build();

    std::vector<VkDescriptorSet> skyDescriptorSets(renderer.getMaxFramesInFlight());
    for (int i = 0; i < renderer.getMaxFramesInFlight(); ++i) {
        skyDescriptorSets[i] = skyDescriptorPool->allocateSet(*skyDescriptorSetLayout);
    }

    skyPipelineBuilder.addDescriptorSetLayout(skyDescriptorSetLayout->getLayout());

    std::unique_ptr<VulkanPipeline> skyGraphicsPipeline = skyPipelineBuilder.buildGraphicsPipeline();

    float metallic = 0.5f;
    float roughness = 0.5f;
    float ao = 1.0f;

    // --- UI init ---
    UIOverlay::CreateInfo uiInfo{};
    uiInfo.window = window.getHandle();
    uiInfo.instance = context.getInstance();
    uiInfo.physicalDevice = context.getPhysicalDevice();
    uiInfo.device = context.getDevice();
    uiInfo.graphicsQueue = graphicsQueue.getQueue();
    uiInfo.msaaSamples = context.getMsaaSamples();
    uiInfo.colorFormat = swapchain.getImageFormat();
    UIOverlay ui(uiInfo);
    ui.setDrawCallback([&]() {
        ImGui::Begin("My Controls");
        ImGui::Text("Frame Time: %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
        if (ImGui::CollapsingHeader("Model Transform")) {
            ImGui::DragFloat3("Position##Model", &modelTransform.position.x, 0.01f);
            ImGui::DragFloat3("Rotation##Model", &modelTransform.rotation.x, 1.0f);
            ImGui::DragFloat3("Scale##Model", &modelTransform.scale.x, 0.01f);
        }
        if (ImGui::CollapsingHeader("Camera Transform")) {
            ImGui::DragFloat3("Position##Camera", &cameraTransform.position.x, 0.1f);
            ImGui::DragFloat3("Rotation##Camera", &cameraTransform.rotation.x, 1.0f);
        }
        if (ImGui::CollapsingHeader("Light Properties")) {
            ImGui::ColorEdit3("Color##Light", &lightParams.color.r);
            ImGui::DragFloat("Intensity##Light", &lightParams.intensity, 0.1f, 0.0f, 1000.0f);
            ImGui::DragFloat3("Direction##Light", &lightParams.direction.x, 0.01f, -1.0f, 1.0f);
        }
        ImGui::DragFloat("Metallic", &metallic, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("Roughness", &roughness, 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat("AO", &ao, 0.01f, 0.0f, 1.0f);
        if (ImGui::CollapsingHeader("Skybox Selection")) {
            for (auto const& [name, skybox_ptr] : skyboxCubemaps) {
                if (ImGui::RadioButton(name.c_str(), currentSkybox == skybox_ptr.get())) {
                    currentSkybox = skybox_ptr.get();
                    currentSkyboxName = name;
                }
            }
        }
        ImGui::End();
    });

    while (!window.shouldClose()) {
        window.pollEvents();

        // handle resize
        if (window.wasResized()) {
            window.resetResizedFlag();
            while (window.getWidth() == 0 || window.getHeight() == 0) {
                window.waitEvents();
                window.pollEvents();
            }
            context.updateFramebufferSize(window.getWidth(), window.getHeight());
            context.framebufferResized = true;
        }
        context.CheckFormatChange(swapchain);

        ui.beginFrame();

        uint32_t size = static_cast<uint32_t>(model.getIndices().size());
        VkCommandBuffer commandBuffer = renderer.beginFrame(swapchain);
        updateUniformBuffer(
            *uniformBuffers[renderer.getCurrentFrame()],
            *skyUniformBuffers[renderer.getCurrentFrame()],
            swapchain.getExtent(),
            modelTransform,
            cameraTransform,
            lightParams,
            metallic,
            roughness,
            ao
        );

        VkDescriptorBufferInfo skyBufferInfo = skyUniformBuffers[renderer.getCurrentFrame()]->getDescriptorInfo();
        VkDescriptorImageInfo skyImageInfo = currentSkybox->getDescriptorInfo();

        DescriptorWriter(context, skyDescriptorSets[renderer.getCurrentFrame()])
            .writeBuffer(0, &skyBufferInfo)
            .writeImage(1, &skyImageInfo)
            .update();


        VkDescriptorBufferInfo bufferInfo = uniformBuffers[renderer.getCurrentFrame()]->getDescriptorInfo();

        VkDescriptorImageInfo imageInfo1 = diffuseImage->getDescriptorInfo();
        VkDescriptorImageInfo imageInfo2 = normalImage->getDescriptorInfo();
        VkDescriptorImageInfo imageInfo3 = emissiveImage->getDescriptorInfo();
        VkDescriptorImageInfo imageInfo4 = msaImage->getDescriptorInfo();

        VkDescriptorImageInfo irradianceInfo = ibl.getIrradianceMap(currentSkyboxName)->getDescriptorInfo();
        VkDescriptorImageInfo prefilteredInfo = ibl.getPrefilteredMap(currentSkyboxName)->getDescriptorInfo();
        VkDescriptorImageInfo brdfLutInfo = ibl.getBrdfLUT()->getDescriptorInfo();
        DescriptorWriter(context, descriptorSets[renderer.getCurrentFrame()])
            .writeBuffer(0, &bufferInfo)
            .writeImage(1, &imageInfo1)
            .writeImage(2, &imageInfo2)
            .writeImage(3, &imageInfo3)
            .writeImage(4, &imageInfo4)
            .writeImage(5, &irradianceInfo)
            .writeImage(6, &prefilteredInfo)
            .writeImage(7, &brdfLutInfo)
            .update();

        renderer.beginRenderPass(commandBuffer, swapchain);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipeline());

        VkViewport viewport{};
        viewport.width = static_cast<float>(swapchain.getExtent().width);
        viewport.height = static_cast<float>(swapchain.getExtent().height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = swapchain.getExtent();
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindIndexBuffer(commandBuffer, indexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            graphicsPipeline->getLayout(),
            0,
            1,
            &descriptorSets[renderer.getCurrentFrame()],
            0,
            nullptr
        );

        VkBuffer vertexBuffers[] = { vertexBuffer.getBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        vkCmdDrawIndexed(commandBuffer, size, 1, 0, 0, 0);

        // skybox
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyGraphicsPipeline->getPipeline());
        vkCmdBindIndexBuffer(commandBuffer, skyboxIndexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            skyGraphicsPipeline->getLayout(),
            0,
            1,
            &skyDescriptorSets[renderer.getCurrentFrame()],
            0,
            nullptr
        );

        VkBuffer skyVertexBuffers[] = { skyboxVertexBuffer.getBuffer() };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, skyVertexBuffers, offsets);

        vkCmdDrawIndexed(commandBuffer, skyboxIndices.size(), 1, 0, 0, 0);

        ui.endFrame(commandBuffer);
        renderer.endRenderPass(commandBuffer, swapchain);
        renderer.endFrame(swapchain, graphicsQueue, presentQueue);
    }

    vkDeviceWaitIdle(context.getDevice());
}
