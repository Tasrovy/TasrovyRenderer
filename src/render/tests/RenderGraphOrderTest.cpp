#include "Pipeline.h"
#include "PipelinePass.h"
#include "RenderGraph.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace Tasrovy::Render;

class TestPipeline final : public PipelineBase {
public:
    TestPipeline()
        : PipelineBase("RenderGraphOrderTest") {
    }

    void GenPass(std::shared_ptr<Scene>) override {
    }

    void texture(const std::string& name) {
        declareTexture({name, PipelineTextureFormat::RGBA16Float});
    }

    void pass(const std::shared_ptr<PipelinePass>& value) {
        addPass(value);
    }

    void buffer(
        const std::string& name,
        bool external = false) {
        declareBuffer({
            name,
            256,
            PipelineBufferUsageTransferSource,
            external,
            external
        });
    }
};

bool expectOrder(
    const RenderGraph& graph,
    const std::vector<std::string>& expected) {
    if (!graph.isValid()) {
        for (const auto& diagnostic : graph.getDiagnostics()) {
            std::cerr << diagnostic << '\n';
        }
        return false;
    }
    if (graph.getNodes().size() != expected.size()) {
        return false;
    }
    for (size_t index = 0; index < expected.size(); ++index) {
        if (graph.getNodes()[index].pass->getName() != expected[index]) {
            std::cerr
                << "Expected pass '" << expected[index]
                << "' at execution index " << index
                << ", got '" << graph.getNodes()[index].pass->getName()
                << "'\n";
            return false;
        }
    }
    return true;
}

bool singleWriterOrderIsDeclarationIndependent() {
    TestPipeline pipeline;
    pipeline.texture("Produced");
    pipeline.texture("Intermediate");
    pipeline.texture("Output");

    auto producer = PipelinePass::create("Producer");
    producer->addColorAttachment("Produced");

    auto middle = PipelinePass::create("Middle");
    middle->addSampledTexture("source", "Produced", 1);
    middle->addColorAttachment("Intermediate");

    auto consumer = PipelinePass::create("Consumer");
    consumer->addSampledTexture("source", "Intermediate", 1);
    consumer->addColorAttachment("Output");

    // Deliberately reverse the data-flow order.
    pipeline.pass(consumer);
    pipeline.pass(middle);
    pipeline.pass(producer);

    return expectOrder(
        RenderGraph::compile(pipeline),
        {"Producer", "Middle", "Consumer"});
}

bool multiWriterVersionChainIsDeclarationIndependent() {
    TestPipeline pipeline;
    pipeline.texture("SceneColor");
    pipeline.texture("Output");

    auto lighting = PipelinePass::create("Lighting");
    lighting->addColorAttachment("SceneColor");

    auto overlay = PipelinePass::create("Overlay");
    overlay->addColorAttachment(
        "SceneColor",
        AttachmentLoad::Load,
        AttachmentStore::Store,
        "Lighting");

    auto consumer = PipelinePass::create("Consumer");
    consumer->addSampledTexture(
        "sceneColor",
        "SceneColor",
        1,
        false,
        "Overlay");
    consumer->addColorAttachment("Output");

    // Deliberately use an unrelated declaration order.
    pipeline.pass(consumer);
    pipeline.pass(lighting);
    pipeline.pass(overlay);

    return expectOrder(
        RenderGraph::compile(pipeline),
        {"Lighting", "Overlay", "Consumer"});
}

bool sampledReadWriteFeedbackIsRejected() {
    TestPipeline pipeline;
    pipeline.texture("Feedback");

    auto feedback = PipelinePass::create("Feedback");
    feedback->addSampledTexture("input", "Feedback", 1);
    feedback->addColorAttachment("Feedback");
    pipeline.pass(feedback);

    const auto graph = RenderGraph::compile(pipeline);
    if (graph.isValid()) {
        std::cerr << "Expected same-pass sampled read/write to be rejected\n";
        return false;
    }
    for (const auto& diagnostic : graph.getDiagnostics()) {
        if (diagnostic.find(
                "as a shader-readable input while modifying it") !=
            std::string::npos) {
            return true;
        }
    }
    std::cerr << "Missing same-pass read/write diagnostic\n";
    return false;
}

bool bufferCopyOrderIsDeclarationIndependent() {
    TestPipeline pipeline;
    pipeline.buffer("Upload", true);
    pipeline.buffer("Intermediate");
    pipeline.buffer("Output");

    auto firstCopy = PipelinePass::create("FirstCopy");
    firstCopy->addCopyCommand({"Upload", "Intermediate", 0, 0, 256});
    auto secondCopy = PipelinePass::create("SecondCopy");
    secondCopy->addCopyCommand({"Intermediate", "Output", 0, 0, 256});

    pipeline.pass(secondCopy);
    pipeline.pass(firstCopy);
    return expectOrder(
        RenderGraph::compile(pipeline),
        {"FirstCopy", "SecondCopy"});
}

bool cyclicGraphHasNoExecutableFallback() {
    TestPipeline pipeline;
    pipeline.texture("AOutput");
    pipeline.texture("BOutput");

    auto passA = PipelinePass::create("PassA");
    passA->addSampledTexture("fromB", "BOutput", 1);
    passA->addColorAttachment("AOutput");

    auto passB = PipelinePass::create("PassB");
    passB->addSampledTexture("fromA", "AOutput", 1);
    passB->addColorAttachment("BOutput");

    pipeline.pass(passA);
    pipeline.pass(passB);

    const auto graph = RenderGraph::compile(pipeline);
    if (graph.isValid()) {
        std::cerr << "Expected cyclic Render Graph to be rejected\n";
        return false;
    }
    if (!graph.getNodes().empty()) {
        std::cerr <<
            "Invalid Render Graph exposed a declaration-order fallback\n";
        return false;
    }
    for (const auto& diagnostic : graph.getDiagnostics()) {
        if (diagnostic.find("dependency cycle") != std::string::npos) {
            return true;
        }
    }
    std::cerr << "Missing Render Graph cycle diagnostic\n";
    return false;
}

} // namespace

int main() {
    if (!singleWriterOrderIsDeclarationIndependent()) {
        return 1;
    }
    if (!multiWriterVersionChainIsDeclarationIndependent()) {
        return 2;
    }
    if (!sampledReadWriteFeedbackIsRejected()) {
        return 3;
    }
    if (!cyclicGraphHasNoExecutableFallback()) {
        return 4;
    }
    if (!bufferCopyOrderIsDeclarationIndependent()) {
        return 5;
    }
    return 0;
}
