#include "../RHIThread.h"

#include <atomic>
#include <cassert>
#include <stdexcept>
#include <vector>

int main() {
    using Tasrovy::Renderer::RHIThread;

    RHIThread thread(2);
    thread.start();
    assert(thread.running());

    std::vector<int> order;
    auto first = thread.submit([&] {
        assert(thread.isCurrentThread());
        order.push_back(1);
    });
    auto second = thread.submit([&] { order.push_back(2); });
    first.get();
    second.get();
    thread.drain();
    assert((order == std::vector<int>{1, 2}));

    auto failure = thread.submit([] {
        throw std::runtime_error("expected worker failure");
    });
    bool propagated = false;
    try {
        failure.get();
    } catch (const std::runtime_error&) {
        propagated = true;
    }
    assert(propagated);

    std::atomic<bool> invoked{false};
    thread.invoke([&] { invoked.store(true, std::memory_order_release); });
    assert(invoked.load(std::memory_order_acquire));

    thread.stop();
    assert(!thread.running());
}
