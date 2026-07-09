#pragma once

#include "Image.h"
#include "TSVector.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Tasrovy::RHI {

enum class RHIAttachmentLoad {
    Clear,
    Load,
    Discard
};

enum class RHIAttachmentStore {
    Store,
    Discard
};

struct RHIAttachmentDesc {
    std::string name;
    std::shared_ptr<Image> image;
    RHIAttachmentLoad load = RHIAttachmentLoad::Clear;
    RHIAttachmentStore store = RHIAttachmentStore::Store;
    bool readOnly = false;
    TSVec4f clearColor = TSVec4f(0.0f, 0.0f, 0.0f, 1.0f);
    float clearDepth = 1.0f;
};

struct PassDesc {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<RHIAttachmentDesc> colorAttachments;
    std::optional<RHIAttachmentDesc> depthAttachment;
};

class Pass : public std::enable_shared_from_this<Pass> {
public:
    static std::shared_ptr<Pass> create(PassDesc desc);

    const PassDesc& getDesc() const;
    const std::string& getName() const;

private:
    explicit Pass(PassDesc desc);

    PassDesc desc_;
};

} // namespace Tasrovy::RHI
