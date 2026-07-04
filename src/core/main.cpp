#include <volk.h>
#include <Dependencies.h>
#include <chrono>
#include <TSMatrix.h>
#include <Logger.hpp>
#include <Window.h>

using namespace Tasrovy;

int main()
{
    if (volkInitialize() != VK_SUCCESS) {
        LOG_CRITICAL("Failed to initialize volk!");
        return -1;
    }
    Tasrovy::Logger::Init();

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

    // Simple triangle
    struct Vertex {
        TSVec3f position;
        TSVec3f color;
    };

    std::vector<Vertex> vertices = {
        {{ 0.0f, -0.5f, 0.0f}, {1.0f, 0.0f, 0.0f}},
        {{ 0.5f,  0.5f, 0.0f}, {0.0f, 1.0f, 0.0f}},
        {{-0.5f,  0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}},
    };
    std::vector<uint32_t> indices = { 0, 1, 2 };

    VulkanBuffer vertexBuffer(context, vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VulkanBuffer indexBuffer(context, indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    immediateSubmitter.copyDataToBuffer(vertices.data(), vertexBuffer, vertices.size() * sizeof(Vertex));
    immediateSubmitter.copyDataToBuffer(indices.data(), indexBuffer, indices.size() * sizeof(uint32_t));

    // Pipeline
    PipelineBuilder pipelineBuilder(context);
    pipelineBuilder.addShaderStage(VK_SHADER_STAGE_VERTEX_BIT, "res\\triangle_vert.spv", "VSMain");
    pipelineBuilder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "res\\triangle_frag.spv", "PSMain");

    VkVertexInputBindingDescription bindingDesc{};
    bindingDesc.binding = 0;
    bindingDesc.stride = sizeof(Vertex);
    bindingDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::array<VkVertexInputAttributeDescription, 2> attrDescs{};
    attrDescs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position) };
    attrDescs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color) };

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDesc;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attrDescs.data();
    pipelineBuilder.setVertexInputState(vertexInput);

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;
    pipelineBuilder.setInputAssemblyState(inputAssembly);
    pipelineBuilder.setRenderingFormats(swapchain.getImageFormat(), context.findDepthFormat());

    auto graphicsPipeline = pipelineBuilder.buildGraphicsPipeline();

    LOG_INFO("Setup complete, entering main loop");

    // Main loop
    while (!window.shouldClose()) {
        window.pollEvents();

        if (window.wasResized()) {
            window.resetResizedFlag();
            while (window.getWidth() == 0 || window.getHeight() == 0) {
                window.waitEvents(); window.pollEvents();
            }
            context.updateFramebufferSize(window.getWidth(), window.getHeight());
            context.framebufferResized = true;
        }
        context.CheckFormatChange(swapchain);

        VkCommandBuffer commandBuffer = renderer.beginFrame(swapchain);

        renderer.beginRenderPass(commandBuffer, swapchain);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline->getPipeline());

        VkViewport viewport{};
        viewport.width = (float)swapchain.getExtent().width;
        viewport.height = (float)swapchain.getExtent().height;
        viewport.minDepth = 0.0f; viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = swapchain.getExtent();
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        VkBuffer vbs[] = { vertexBuffer.getBuffer() };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vbs, offsets);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer.getBuffer(), 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, (uint32_t)indices.size(), 1, 0, 0, 0);

        renderer.endRenderPass(commandBuffer, swapchain);
        renderer.endFrame(swapchain, graphicsQueue, presentQueue);
    }

    vkDeviceWaitIdle(context.getDevice());
}
