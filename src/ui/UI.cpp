#include "UI.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

UIOverlay::UIOverlay(const CreateInfo& info)
    : _device(info.device)
{
    // 1. 创建 ImGui 描述符池
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 10 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 10 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 10 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 10 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 10 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 10 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 10 }
    };

    VkDescriptorPoolCreateInfo pool_info = {};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    VkResult result = vkCreateDescriptorPool(info.device, &pool_info, nullptr, &_descriptorPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }

    // 2. 初始化 ImGui 上下文
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // 3. 设置 ImGui 样式
    ImGui::StyleColorsDark();

    // 4. 初始化 ImGui 后端
    ImGui_ImplGlfw_InitForVulkan(info.window, true);

    ImGui_ImplVulkan_InitInfo init_info = {};
    init_info.Instance = info.instance;
    init_info.PhysicalDevice = info.physicalDevice;
    init_info.Device = info.device;
    init_info.Queue = info.graphicsQueue;
    init_info.DescriptorPool = _descriptorPool;
    init_info.MinImageCount = info.minImageCount;
    init_info.ImageCount = info.imageCount;
    init_info.PipelineInfoMain.MSAASamples = info.msaaSamples;

    VkPipelineRenderingCreateInfo rendering_info = {};
    rendering_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering_info.colorAttachmentCount = 1;
    rendering_info.pColorAttachmentFormats = &info.colorFormat;

    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering_info;
    init_info.UseDynamicRendering = true;

    ImGui_ImplVulkan_Init(&init_info);
}

UIOverlay::~UIOverlay() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
}

void UIOverlay::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (_drawCallback) {
        _drawCallback();
    }
}

void UIOverlay::endFrame(VkCommandBuffer cmd) {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
}
