#include "Pipeline.h"
#include "ResourceBackend.h"

#include <stdexcept>
#include <utility>

namespace Tasrovy::RHI {

struct Pipeline::Impl {
    std::unique_ptr<IPipelineBackend> backend;
};

Pipeline::~Pipeline() = default;

std::shared_ptr<Pipeline> Pipeline::CreateFromBackend(
    std::unique_ptr<IPipelineBackend> backend) {
    if (!backend) throw std::invalid_argument("Pipeline backend is null");
    auto pipeline = std::shared_ptr<Pipeline>(new Pipeline());
    pipeline->impl_ = std::make_unique<Impl>();
    pipeline->impl_->backend = std::move(backend);
    return pipeline;
}

IPipelineBackend& Pipeline::backend() { return *impl_->backend; }
const IPipelineBackend& Pipeline::backend() const { return *impl_->backend; }

} // namespace Tasrovy::RHI
