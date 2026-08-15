#pragma once

#include <cstddef>
#include <memory>
#include <vector>

namespace Tasrovy::RHI { class Buffer; }

namespace Tasrovy::Renderer {

// CPU-owned immutable bytes produced by the render thread and uploaded only
// after the RHI thread has acquired a fence-safe frame slot.
struct FrameBufferUpload {
    std::shared_ptr<RHI::Buffer> buffer;
    std::vector<std::byte> bytes;
};

} // namespace Tasrovy::Renderer
