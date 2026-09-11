#pragma once

#include <memory>

namespace Tasrovy::Render {

class Shader;

// API-independent graphics shader pair used by one material technique slot.
// Either stage may be omitted and inherited from the owning PipelinePass.
struct MaterialShader {
    std::shared_ptr<Shader> vertexShader;
    std::shared_ptr<Shader> fragmentShader;

    bool empty() const {
        return !vertexShader && !fragmentShader;
    }
};

} // namespace Tasrovy::Render
