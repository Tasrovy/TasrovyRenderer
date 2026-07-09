#include "Pass.h"
#include <utility>

namespace Tasrovy::RHI {

std::shared_ptr<Pass> Pass::create(PassDesc desc) {
    return std::shared_ptr<Pass>(new Pass(std::move(desc)));
}

Pass::Pass(PassDesc desc)
    : desc_(std::move(desc)) {
}

const PassDesc& Pass::getDesc() const {
    return desc_;
}

const std::string& Pass::getName() const {
    return desc_.name;
}

} // namespace Tasrovy::RHI
