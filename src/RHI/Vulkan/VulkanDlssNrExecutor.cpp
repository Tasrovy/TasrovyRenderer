#include "VulkanDlssNrExecutor.h"

#include "TasrovyDlssNrConfig.h"
#include "VulkanConversions.h"
#include "../CommandList.h"
#include "../Image.h"
#include "../RHIBackendAccess.h"

#include <volk.h>
#include <nvsdk_ngx_helpers_vk.h>
#include <nvsdk_ngx_params.h>

#include <Logger.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <system_error>
#include <utility>

namespace Tasrovy::RHI::Vulkan {
namespace {

constexpr auto NeuralRenderingFeature =
    static_cast<NVSDK_NGX_Feature>(18);
constexpr const char* ProjectId =
    "6f85c148-0af7-4fb8-95b9-5767c24f35dc";

struct NgxVulkanApi {
    using InitProjectIdExt = NVSDK_NGX_Result (NVSDK_CONV *)(
        const char*, NVSDK_NGX_EngineType, const char*, const wchar_t*,
        VkInstance, VkPhysicalDevice, VkDevice,
        PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
        NVSDK_NGX_Version, const NVSDK_NGX_FeatureCommonInfo*);

    decltype(&NVSDK_NGX_VULKAN_RequiredExtensions) requiredExtensions = nullptr;
    InitProjectIdExt init = nullptr;
    decltype(&NVSDK_NGX_VULKAN_Shutdown1) shutdown = nullptr;
    decltype(&NVSDK_NGX_VULKAN_GetCapabilityParameters)
        getCapabilityParameters = nullptr;
    decltype(&NVSDK_NGX_VULKAN_CreateFeature) createFeature = nullptr;
    decltype(&NVSDK_NGX_VULKAN_ReleaseFeature) releaseFeature = nullptr;
    decltype(&NVSDK_NGX_VULKAN_EvaluateFeature) evaluateFeature = nullptr;

    bool complete() const {
        return requiredExtensions && init && shutdown &&
            getCapabilityParameters && createFeature && releaseFeature &&
            evaluateFeature;
    }
};

template <typename Function>
Function loadFunction(HMODULE module, const char* name) {
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

NgxVulkanApi loadApi(HMODULE module) {
    NgxVulkanApi api;
    api.requiredExtensions = loadFunction<decltype(api.requiredExtensions)>(
        module, "NVSDK_NGX_VULKAN_RequiredExtensions");
    api.init = loadFunction<decltype(api.init)>(
        module, "NVSDK_NGX_VULKAN_Init_ProjectID_Ext");
    api.shutdown = loadFunction<decltype(api.shutdown)>(
        module, "NVSDK_NGX_VULKAN_Shutdown1");
    api.getCapabilityParameters =
        loadFunction<decltype(api.getCapabilityParameters)>(
            module, "NVSDK_NGX_VULKAN_GetCapabilityParameters");
    api.createFeature = loadFunction<decltype(api.createFeature)>(
        module, "NVSDK_NGX_VULKAN_CreateFeature");
    api.releaseFeature = loadFunction<decltype(api.releaseFeature)>(
        module, "NVSDK_NGX_VULKAN_ReleaseFeature");
    api.evaluateFeature = loadFunction<decltype(api.evaluateFeature)>(
        module, "NVSDK_NGX_VULKAN_EvaluateFeature");
    return api;
}

std::filesystem::path runtimeLibraryPath() {
    return std::filesystem::path(TASROVY_DLSS_NR_RUNTIME_DIR) /
        "nvngx_dlssnr.dll";
}

std::filesystem::path executableDirectory() {
    std::array<wchar_t, 32768> executablePath{};
    const auto length = GetModuleFileNameW(
        nullptr, executablePath.data(),
        static_cast<DWORD>(executablePath.size()));
    if (length == 0 || length >= executablePath.size())
        return {};
    return std::filesystem::path(executablePath.data()).parent_path();
}

struct DlssNrBridgeApi {
    using Create = NVSDK_NGX_Result (NVSDK_CONV *)(
        const wchar_t*, const wchar_t*, VkInstance, VkPhysicalDevice,
        VkDevice, PFN_vkGetInstanceProcAddr, PFN_vkGetDeviceProcAddr,
        VkCommandBuffer, NVSDK_NGX_Parameter*, uint32_t, uint32_t,
        uint32_t, float, float, float, float, uint32_t, uint32_t,
        NVSDK_NGX_Handle**);
    using Evaluate = NVSDK_NGX_Result (NVSDK_CONV *)(
        VkCommandBuffer, NVSDK_NGX_Handle*, NVSDK_NGX_Parameter*,
        NVSDK_NGX_Resource_VK*, NVSDK_NGX_Resource_VK*,
        NVSDK_NGX_Resource_VK*, NVSDK_NGX_Resource_VK*,
        uint32_t, uint32_t, float, float, uint32_t, uint32_t);
    using Release = void (NVSDK_CONV *)(NVSDK_NGX_Handle*);
    using Shutdown = void (NVSDK_CONV *)();
    using LastResult = uint32_t (NVSDK_CONV *)();

    Create create = nullptr;
    Evaluate evaluate = nullptr;
    Release release = nullptr;
    Shutdown shutdown = nullptr;
    LastResult lastInitResult = nullptr;
    LastResult lastCreateResult = nullptr;

    bool complete() const {
        return create && evaluate && release && shutdown;
    }
};

DlssNrBridgeApi loadBridgeApi(HMODULE module) {
    DlssNrBridgeApi bridge;
    bridge.create = loadFunction<decltype(bridge.create)>(
        module, "tasrovyDlssNrVulkanCreate");
    bridge.evaluate = loadFunction<decltype(bridge.evaluate)>(
        module, "tasrovyDlssNrVulkanEvaluate");
    bridge.release = loadFunction<decltype(bridge.release)>(
        module, "tasrovyDlssNrVulkanRelease");
    bridge.shutdown = loadFunction<decltype(bridge.shutdown)>(
        module, "tasrovyDlssNrVulkanShutdown");
    bridge.lastInitResult = loadFunction<decltype(bridge.lastInitResult)>(
        module, "tasrovyDlssNrVulkanLastInitResult");
    bridge.lastCreateResult = loadFunction<decltype(bridge.lastCreateResult)>(
        module, "tasrovyDlssNrVulkanLastCreateResult");
    return bridge;
}

HMODULE loadNgxCoreModule() {
    constexpr std::array<const wchar_t*, 2> registryKeys{
        L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\NGXCore",
        L"SYSTEM\\CurrentControlSet\\Services\\nvlddmkm\\Parameters\\NGXCore"
    };
    for (const auto* key : registryKeys) {
        std::array<wchar_t, 1024> driverPath{};
        DWORD bytes = static_cast<DWORD>(
            driverPath.size() * sizeof(driverPath.front()));
        const auto status = RegGetValueW(
            HKEY_LOCAL_MACHINE,
            key,
            L"NGXPath",
            RRF_RT_REG_SZ,
            nullptr,
            driverPath.data(),
            &bytes);
        if (status != ERROR_SUCCESS)
            continue;
        const auto corePath = std::filesystem::path(driverPath.data()) /
            "_nvngx.dll";
        if (const auto module = LoadLibraryW(corePath.c_str()))
            return module;
    }
    return nullptr;
}

struct ModelConfiguration {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t style = 0;
    float intensity = 1.0f;
    float localTone = 1.0f;
    float localStructure = 1.0f;
    float skinStructure = -1.0f;
    uint32_t autoMask = 0;
    uint32_t uiCorrection = 0;

    bool operator==(const ModelConfiguration&) const = default;
};

NVSDK_NGX_Resource_VK imageResource(
    const Image& image, uint32_t width, uint32_t height, bool readWrite) {
    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.baseMipLevel = 0;
    range.levelCount = 1;
    range.baseArrayLayer = 0;
    range.layerCount = 1;
    return NVSDK_NGX_Create_ImageView_Resource_VK(
        reinterpret_cast<VkImageView>(BackendAccess::imageView(image)),
        reinterpret_cast<VkImage>(BackendAccess::image(image)),
        range,
        toVkFormat(image.getFormat()),
        width,
        height,
        readWrite);
}

} // namespace

DlssNrExtensionRequirements queryDlssNrExtensionRequirements() {
    DlssNrExtensionRequirements requirements;
    const auto module = loadNgxCoreModule();
    if (!module) {
        LOG_WARN("DLSS-NR: failed to load NVIDIA NGX Core for extension discovery");
        return requirements;
    }
    const auto api = loadApi(module);
    if (!api.requiredExtensions) {
        LOG_WARN("DLSS-NR: runtime does not export Vulkan extension discovery");
        FreeLibrary(module);
        return requirements;
    }
    unsigned int instanceCount = 0;
    unsigned int deviceCount = 0;
    const char** instanceNames = nullptr;
    const char** deviceNames = nullptr;
    const auto result = api.requiredExtensions(
        &instanceCount, &instanceNames, &deviceCount, &deviceNames);
    if (!NVSDK_NGX_SUCCEED(result)) {
        LOG_WARN(
            "DLSS-NR: NGX extension query failed (0x{:08x}); integration disabled",
            static_cast<uint32_t>(result));
        FreeLibrary(module);
        return requirements;
    }
    for (unsigned int index = 0; index < instanceCount; ++index) {
        if (instanceNames[index])
            requirements.instance.emplace_back(instanceNames[index]);
    }
    for (unsigned int index = 0; index < deviceCount; ++index) {
        if (deviceNames[index])
            requirements.device.emplace_back(deviceNames[index]);
    }
    LOG_INFO(
        "DLSS-NR: requested {} Vulkan instance and {} device extensions",
        requirements.instance.size(), requirements.device.size());
    FreeLibrary(module);
    return requirements;
}

struct VulkanDlssNrExecutor::Impl {
    explicit Impl(const BackendInteropContext& interop)
        : instance(reinterpret_cast<VkInstance>(interop.handles[0])),
          physicalDevice(
              reinterpret_cast<VkPhysicalDevice>(interop.handles[1])),
          device(reinterpret_cast<VkDevice>(interop.handles[2])) {
        module = loadNgxCoreModule();
        if (module)
            api = loadApi(module);
        const auto bridgePath = executableDirectory() /
            TASROVY_DLSS_NR_BRIDGE_NAME;
        bridgeModule = LoadLibraryW(bridgePath.c_str());
        if (bridgeModule)
            bridge = loadBridgeApi(bridgeModule);
    }

    ~Impl() {
        releaseFeature();
        if (bridge.complete())
            bridge.shutdown();
        if (bridgeModule) {
            FreeLibrary(bridgeModule);
            bridgeModule = nullptr;
        }
        parameters = nullptr;
        if (initialized) {
            api.shutdown(device);
            initialized = false;
        }
        if (module) {
            FreeLibrary(module);
            module = nullptr;
        }
    }

    bool initialize() {
        if (initialized)
            return true;
        if (initializationAttempted)
            return false;
        initializationAttempted = true;
        if (!module || !api.complete()) {
            LOG_ERROR(
                "DLSS-NR: configured DLL is missing one or more NGX Vulkan exports");
            return false;
        }

        const auto runtimePath =
            std::filesystem::path(TASROVY_DLSS_NR_RUNTIME_DIR);
        const auto runtimePathWide = runtimePath.wstring();
        const wchar_t* searchPaths[] = {runtimePathWide.c_str()};

        std::error_code filesystemError;
        auto applicationDataDirectory = std::filesystem::temp_directory_path(
            filesystemError);
        if (filesystemError)
            applicationDataDirectory = std::filesystem::current_path();
        applicationDataDirectory /= "TasrovyRenderer/NGX";
        std::filesystem::create_directories(
            applicationDataDirectory, filesystemError);
        applicationDataPath = applicationDataDirectory.wstring();

        NVSDK_NGX_FeatureCommonInfo featureInfo{};
        featureInfo.PathListInfo.Path = searchPaths;
        featureInfo.PathListInfo.Length = 1;
        const auto result = api.init(
            ProjectId,
            NVSDK_NGX_ENGINE_TYPE_CUSTOM,
            "1.0",
            applicationDataPath.c_str(),
            instance,
            physicalDevice,
            device,
            vkGetInstanceProcAddr,
            vkGetDeviceProcAddr,
            NVSDK_NGX_Version_API,
            &featureInfo);
        if (!NVSDK_NGX_SUCCEED(result)) {
            LOG_ERROR(
                "DLSS-NR: NGX initialization failed (0x{:08x}); using fallback",
                static_cast<uint32_t>(result));
            return false;
        }
        initialized = true;

        const auto parameterResult =
            api.getCapabilityParameters(&parameters);
        if (!NVSDK_NGX_SUCCEED(parameterResult) || !parameters) {
            LOG_ERROR(
                "DLSS-NR: capability parameter query failed (0x{:08x}); using fallback",
                static_cast<uint32_t>(parameterResult));
            api.shutdown(device);
            initialized = false;
            return false;
        }
        LOG_INFO("DLSS-NR: NGX Vulkan runtime initialized from '{}'",
            runtimePath.string());
        return true;
    }

    void releaseFeature() noexcept {
        if (feature) {
            if (bridge.complete())
                bridge.release(feature);
            feature = nullptr;
        }
        configuration.reset();
        resetHistory = true;
    }

    bool ensureFeature(
        VkCommandBuffer commandBuffer,
        const ModelConfiguration& desired) {
        if (feature && configuration && *configuration == desired)
            return true;
        if (featureCreationDisabled)
            return false;
        releaseFeature();
        if (!bridge.complete()) {
            LOG_ERROR(
                "DLSS-NR: Vulkan caller bridge is missing; using fallback");
            featureCreationDisabled = true;
            return false;
        }
        if (!corePrimed) {
            NVSDK_NGX_Handle* primeFeature = nullptr;
            const auto primeResult = api.createFeature(
                commandBuffer,
                NeuralRenderingFeature,
                parameters,
                &primeFeature);
            if (primeFeature)
                api.releaseFeature(primeFeature);
            LOG_INFO(
                "DLSS-NR: primed NGX Core for Feature 18 (0x{:08x})",
                static_cast<uint32_t>(primeResult));
            corePrimed = true;
        }
        const auto runtimePath = runtimeLibraryPath().wstring();
        const auto result = bridge.create(
            runtimePath.c_str(),
            applicationDataPath.c_str(),
            instance,
            physicalDevice,
            device,
            vkGetInstanceProcAddr,
            vkGetDeviceProcAddr,
            commandBuffer,
            parameters,
            desired.width,
            desired.height,
            desired.style,
            desired.intensity,
            desired.localTone,
            desired.localStructure,
            desired.skinStructure,
            desired.autoMask,
            desired.uiCorrection,
            &feature);
        if (!NVSDK_NGX_SUCCEED(result) || !feature) {
            const auto initResult = bridge.lastInitResult
                ? bridge.lastInitResult() : 0u;
            const auto createResult = bridge.lastCreateResult
                ? bridge.lastCreateResult() : 0u;
            LOG_ERROR(
                "DLSS-NR: bridge feature creation failed (result=0x{:08x}, init=0x{:08x}, create=0x{:08x}); using fallback",
                static_cast<uint32_t>(result), initResult, createResult);
            feature = nullptr;
            featureCreationDisabled = true;
            return false;
        }
        configuration = desired;
        resetHistory = true;
        LOG_INFO("DLSS-NR: created same-resolution {}x{} feature",
            desired.width, desired.height);
        return true;
    }

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    HMODULE module = nullptr;
    NgxVulkanApi api;
    HMODULE bridgeModule = nullptr;
    DlssNrBridgeApi bridge;
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    std::optional<ModelConfiguration> configuration;
    bool initializationAttempted = false;
    bool initialized = false;
    bool resetHistory = true;
    bool evaluationDisabled = false;
    bool featureCreationDisabled = false;
    bool corePrimed = false;
    std::wstring applicationDataPath;
};

VulkanDlssNrExecutor::VulkanDlssNrExecutor(
    const BackendInteropContext& interop)
    : impl_(std::make_unique<Impl>(interop)) {}

VulkanDlssNrExecutor::~VulkanDlssNrExecutor() = default;

void VulkanDlssNrExecutor::invalidateResources() noexcept {
    if (impl_) {
        impl_->releaseFeature();
        impl_->featureCreationDisabled = false;
    }
}

bool VulkanDlssNrExecutor::tryExecute(
    const ExternalFeatureExecuteContext& context) noexcept {
    if (!impl_ || impl_->evaluationDisabled || !context.commandList ||
        !context.inputColor || !context.motionVectors || !context.depth ||
        !context.outputColor || context.width == 0 || context.height == 0) {
        return false;
    }

    try {
        if (!impl_->initialize())
            return false;

        std::array<float, 8> values{
            0.0f, 1.0f, 1.0f, 1.0f,
            -1.0f, 0.0f, 0.0f, 0.0f
        };
        if (context.parameters.size() >= sizeof(values)) {
            std::memcpy(values.data(), context.parameters.data(), sizeof(values));
        }
        const ModelConfiguration desired{
            context.width,
            context.height,
            static_cast<uint32_t>(values[0]),
            values[1],
            values[2],
            values[3],
            values[4],
            values[5] != 0.0f ? 1u : 0u,
            values[6] != 0.0f ? 1u : 0u
        };

        const auto commandBuffer = reinterpret_cast<VkCommandBuffer>(
            BackendAccess::commandBuffer(*context.commandList));
        if (commandBuffer == VK_NULL_HANDLE ||
            !impl_->ensureFeature(commandBuffer, desired)) {
            return false;
        }

        auto color = imageResource(
            *context.inputColor, context.width, context.height, true);
        auto motion = imageResource(
            *context.motionVectors, context.width, context.height, true);
        auto depth = imageResource(
            *context.depth, context.width, context.height, true);
        auto output = imageResource(
            *context.outputColor, context.width, context.height, true);

        context.commandList->transitionImage(
            *context.inputColor,
            ImageLayout::ShaderRead,
            ImageLayout::General);
        context.commandList->transitionImage(
            *context.motionVectors,
            ImageLayout::ShaderRead,
            ImageLayout::General);
        context.commandList->transitionImage(
            *context.depth,
            ImageLayout::ShaderRead,
            ImageLayout::General);
        context.commandList->transitionImage(
            *context.outputColor,
            ImageLayout::ColorAttachment,
            ImageLayout::General);
        const auto result = impl_->bridge.evaluate(
            commandBuffer,
            impl_->feature,
            impl_->parameters,
            &color,
            &depth,
            &motion,
            &output,
            context.width,
            context.height,
            context.motionVectorScaleX,
            context.motionVectorScaleY,
            context.depthInverted ? 1u : 0u,
            (impl_->resetHistory || values[7] != 0.0f) ? 1u : 0u);
        context.commandList->transitionImage(
            *context.inputColor,
            ImageLayout::General,
            ImageLayout::ShaderRead);
        context.commandList->transitionImage(
            *context.motionVectors,
            ImageLayout::General,
            ImageLayout::ShaderRead);
        context.commandList->transitionImage(
            *context.depth,
            ImageLayout::General,
            ImageLayout::ShaderRead);
        context.commandList->transitionImage(
            *context.outputColor,
            ImageLayout::General,
            ImageLayout::ColorAttachment);

        if (!NVSDK_NGX_SUCCEED(result)) {
            LOG_ERROR(
                "DLSS-NR: evaluation failed (0x{:08x}); disabling NGX for subsequent frames",
                static_cast<uint32_t>(result));
            // NGX may already have recorded commands. Treat this pass as consumed
            // for the current frame; the normal fallback resumes next frame.
            impl_->evaluationDisabled = true;
            impl_->resetHistory = true;
            return true;
        }
        impl_->resetHistory = false;
        return true;
    } catch (const std::exception& error) {
        LOG_ERROR("DLSS-NR: Vulkan executor exception: {}", error.what());
    } catch (...) {
        LOG_ERROR("DLSS-NR: Vulkan executor failed with an unknown exception");
    }
    return false;
}

} // namespace Tasrovy::RHI::Vulkan
