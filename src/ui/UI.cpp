#include "UI.h"
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <GLFW/glfw3.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include "Logger.hpp"

namespace Tasrovy::UI {

static void check_vk_result(VkResult err)
{
    if (err == VK_SUCCESS) return;
    LOG_ERROR("[vulkan] VkResult = {}", static_cast<int>(err));
    if (err < 0) abort();
}

UIOverlay::UIOverlay(const CreateInfo& info)
    : _window(info.window)
    , _device(info.device)
    , _colorFormat(info.colorFormat)
{
    LOG_INFO("UIOverlay: creating descriptor pool");
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

    VkDescriptorPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pool_info.maxSets = 1000;
    pool_info.poolSizeCount = std::size(pool_sizes);
    pool_info.pPoolSizes = pool_sizes;

    if (vkCreateDescriptorPool(_device, &pool_info, nullptr, &_descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }
    LOG_INFO("UIOverlay: descriptor pool created");

    // Init ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    // Init backends
    ImGui_ImplGlfw_InitForVulkan(info.window, true);

    // Load Vulkan functions via volk
    static VkInstance s_instance = VK_NULL_HANDLE;
    static VkDevice   s_device   = VK_NULL_HANDLE;
    s_instance = info.instance;
    s_device   = info.device;

    ImGui_ImplVulkan_LoadFunctions(VK_API_VERSION_1_3,
        [](const char* name, void*) -> PFN_vkVoidFunction {
            auto addr = vkGetInstanceProcAddr(s_instance, name);
            if (addr) return addr;
            if (s_device) return vkGetDeviceProcAddr(s_device, name);
            return nullptr;
        }, nullptr);

    // IO config
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    // DPI scaling
    float mainScale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(mainScale);
    style.FontScaleDpi = mainScale;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Use dynamic rendering (no VkRenderPass needed)
    VkPipelineRenderingCreateInfoKHR renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &_colorFormat;

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = info.instance;
    init_info.PhysicalDevice = info.physicalDevice;
    init_info.Device = info.device;
    init_info.QueueFamily = info.queueFamily;
    init_info.Queue = info.graphicsQueue;
    init_info.DescriptorPool = _descriptorPool;
    init_info.MinImageCount = info.minImageCount;
    init_info.ImageCount = info.imageCount;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    init_info.PipelineInfoMain.MSAASamples = info.msaaSamples;
    init_info.UseDynamicRendering = true;
    init_info.Allocator = nullptr;
    init_info.CheckVkResultFn = check_vk_result;

    LOG_INFO("UIOverlay: calling ImGui_ImplVulkan_Init");
    ImGui_ImplVulkan_Init(&init_info);
    LOG_INFO("UIOverlay: initialization complete");
}

UIOverlay::~UIOverlay() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    vkDestroyDescriptorPool(_device, _descriptorPool, nullptr);
}

bool UIOverlay::beginFrame() {
    int w, h;
    glfwGetFramebufferSize(_window, &w, &h);
    if (w == 0 || h == 0) {
        ImGui_ImplGlfw_Sleep(10);
        return false;
    }
    ImGui::GetIO().DisplaySize = ImVec2((float)w, (float)h);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (_drawCallback) _drawCallback();
    return true;
}

void UIOverlay::endFrame(VkCommandBuffer cmd, VkImageView colorView, VkExtent2D extent) {
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f)
        return;

    // Dynamic rendering: render ImGui on top of existing content
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = colorView;
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderInfo.renderArea = { {0, 0}, extent };
    renderInfo.layerCount = 1;
    renderInfo.colorAttachmentCount = 1;
    renderInfo.pColorAttachments = &colorAttachment;

    vkCmdBeginRendering(cmd, &renderInfo);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    vkCmdEndRendering(cmd);

    // Update and render secondary viewports
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

void UIOverlay::renderDrawData(VkCommandBuffer cmd) {
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
        return;
    }

    ImGui_ImplVulkan_RenderDrawData(drawData, cmd);

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

} // namespace Tasrovy::UI
