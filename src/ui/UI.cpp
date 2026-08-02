#include "UI.h"
#include <volk.h>
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
    const auto instance = reinterpret_cast<VkInstance>(info.instance);
    const auto physicalDevice =
        reinterpret_cast<VkPhysicalDevice>(info.physicalDevice);
    const auto device = reinterpret_cast<VkDevice>(info.device);
    const auto graphicsQueue =
        reinterpret_cast<VkQueue>(info.graphicsQueue);
    const auto colorFormat = static_cast<VkFormat>(info.colorFormat);
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

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create ImGui descriptor pool!");
    }
    _descriptorPool =
        reinterpret_cast<uint64_t>(descriptorPool);
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
    s_instance = instance;
    s_device   = device;

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
    // Multi-viewport creates additional GLFW windows and Vulkan swapchains.
    // Keep it disabled until those windows are integrated with the main-thread
    // event loop and FrameScheduler submission/synchronization path.

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
    renderingInfo.pColorAttachmentFormats = &colorFormat;

    ImGui_ImplVulkan_InitInfo init_info{};
    init_info.Instance = instance;
    init_info.PhysicalDevice = physicalDevice;
    init_info.Device = device;
    init_info.QueueFamily = info.queueFamily;
    init_info.Queue = graphicsQueue;
    init_info.DescriptorPool = descriptorPool;
    init_info.MinImageCount = info.minImageCount;
    init_info.ImageCount = info.imageCount;
    init_info.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    init_info.PipelineInfoMain.MSAASamples =
        static_cast<VkSampleCountFlagBits>(info.msaaSamples);
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
    vkDestroyDescriptorPool(
        reinterpret_cast<VkDevice>(_device),
        reinterpret_cast<VkDescriptorPool>(_descriptorPool),
        nullptr);
}

bool UIOverlay::beginFrame(uint32_t framebufferWidth, uint32_t framebufferHeight) {
    if (framebufferWidth == 0 || framebufferHeight == 0) {
        return false;
    }
    ImGui::GetIO().DisplaySize = ImVec2(
        static_cast<float>(framebufferWidth),
        static_cast<float>(framebufferHeight));

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (_drawCallback) _drawCallback();
    return true;
}

void UIOverlay::renderDrawData(uint64_t nativeCommandBuffer) {
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f) {
        return;
    }

    ImGui_ImplVulkan_RenderDrawData(
        drawData,
        reinterpret_cast<VkCommandBuffer>(nativeCommandBuffer));

    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }
}

} // namespace Tasrovy::UI
