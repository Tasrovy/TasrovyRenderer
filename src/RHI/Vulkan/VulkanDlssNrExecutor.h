#pragma once

#include "../Device.h"
#include "../ExternalFeatureExecutor.h"

#include <memory>
#include <string>
#include <vector>

namespace Tasrovy::RHI::Vulkan {

struct DlssNrExtensionRequirements {
    std::vector<std::string> instance;
    std::vector<std::string> device;
};

DlssNrExtensionRequirements queryDlssNrExtensionRequirements();

class VulkanDlssNrExecutor final : public IExternalFeatureExecutor {
public:
    explicit VulkanDlssNrExecutor(const BackendInteropContext& interop);
    ~VulkanDlssNrExecutor() override;

    void invalidateResources() noexcept override;
    bool tryExecute(
        const ExternalFeatureExecuteContext& context) noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace Tasrovy::RHI::Vulkan
