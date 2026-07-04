#include "Model.hpp"
#include <cmath>

namespace Tasrovy {

void Model::SetData(std::vector<Vertex> verts, std::vector<uint32_t> inds) {
    vertices = std::move(verts);
    indices = std::move(inds);
}

std::unique_ptr<Model> Model::GenCube() {
    auto model = std::make_unique<Model>();

    const float h = 0.5f;

    // Each face has 4 unique vertices (sharing would require different normals/UVs)
    // position, normal, tangent, color, uv0
    auto V3 = [](float x, float y, float z) { return Tasrovy::TSVec3f(x, y, z); };
    auto V2 = [](float x, float y) { return Tasrovy::TSVec2f(x, y); };
    auto V2Zero = []() { return Tasrovy::TSVec2f(0.0f, 0.0f); };

    model->vertices = {
        // Front (+Z)
        { V3(-h,-h, h), V3(0,0,1), V3(1,0,0), V3(1,1,1), V2(0,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h,-h, h), V3(0,0,1), V3(1,0,0), V3(1,1,1), V2(1,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h, h, h), V3(0,0,1), V3(1,0,0), V3(1,1,1), V2(1,1), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h, h, h), V3(0,0,1), V3(1,0,0), V3(1,1,1), V2(0,1), V2Zero(), V2Zero(), V2Zero() },
        // Back (-Z)
        { V3( h,-h,-h), V3(0,0,-1), V3(-1,0,0), V3(1,1,1), V2(0,0), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h,-h,-h), V3(0,0,-1), V3(-1,0,0), V3(1,1,1), V2(1,0), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h, h,-h), V3(0,0,-1), V3(-1,0,0), V3(1,1,1), V2(1,1), V2Zero(), V2Zero(), V2Zero() },
        { V3( h, h,-h), V3(0,0,-1), V3(-1,0,0), V3(1,1,1), V2(0,1), V2Zero(), V2Zero(), V2Zero() },
        // Right (+X)
        { V3( h,-h, h), V3(1,0,0), V3(0,0,1), V3(1,1,1), V2(0,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h,-h,-h), V3(1,0,0), V3(0,0,1), V3(1,1,1), V2(1,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h, h,-h), V3(1,0,0), V3(0,0,1), V3(1,1,1), V2(1,1), V2Zero(), V2Zero(), V2Zero() },
        { V3( h, h, h), V3(1,0,0), V3(0,0,1), V3(1,1,1), V2(0,1), V2Zero(), V2Zero(), V2Zero() },
        // Left (-X)
        { V3(-h,-h,-h), V3(-1,0,0), V3(0,0,-1), V3(1,1,1), V2(0,0), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h,-h, h), V3(-1,0,0), V3(0,0,-1), V3(1,1,1), V2(1,0), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h, h, h), V3(-1,0,0), V3(0,0,-1), V3(1,1,1), V2(1,1), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h, h,-h), V3(-1,0,0), V3(0,0,-1), V3(1,1,1), V2(0,1), V2Zero(), V2Zero(), V2Zero() },
        // Top (+Y)
        { V3(-h, h, h), V3(0,1,0), V3(1,0,0), V3(1,1,1), V2(0,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h, h, h), V3(0,1,0), V3(1,0,0), V3(1,1,1), V2(1,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h, h,-h), V3(0,1,0), V3(1,0,0), V3(1,1,1), V2(1,1), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h, h,-h), V3(0,1,0), V3(1,0,0), V3(1,1,1), V2(0,1), V2Zero(), V2Zero(), V2Zero() },
        // Bottom (-Y)
        { V3(-h,-h,-h), V3(0,-1,0), V3(1,0,0), V3(1,1,1), V2(0,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h,-h,-h), V3(0,-1,0), V3(1,0,0), V3(1,1,1), V2(1,0), V2Zero(), V2Zero(), V2Zero() },
        { V3( h,-h, h), V3(0,-1,0), V3(1,0,0), V3(1,1,1), V2(1,1), V2Zero(), V2Zero(), V2Zero() },
        { V3(-h,-h, h), V3(0,-1,0), V3(1,0,0), V3(1,1,1), V2(0,1), V2Zero(), V2Zero(), V2Zero() },
    };

    model->indices = {
        0,1,2, 2,3,0,       // front
        4,5,6, 6,7,4,       // back
        8,9,10, 10,11,8,    // right
        12,13,14, 14,15,12, // left
        16,17,18, 18,19,16, // top
        20,21,22, 22,23,20  // bottom
    };

    return model;
}

std::unique_ptr<Model> Model::GenSphere(uint32_t sectors, uint32_t stacks) {
    auto model = std::make_unique<Model>();

    for (uint32_t s = 0; s < sectors; ++s) {
        for (uint32_t t = 0; t < stacks; ++t) {
            float u0 = (float)s / (float)sectors;
            float u1 = (float)(s + 1) / (float)sectors;
            float v0 = (float)t / (float)stacks;
            float v1 = (float)(t + 1) / (float)stacks;

            float theta0 = u0 * two_pi<float>();
            float theta1 = u1 * two_pi<float>();
            float phi0 = v0 * pi<float>();
            float phi1 = v1 * pi<float>();

            auto spherePoint = [](float theta, float phi) -> TSVec3f {
                return TSVec3f(
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi),
                    std::sin(phi) * std::sin(theta)
                );
            };

            TSVec3f p00 = spherePoint(theta0, phi0);
            TSVec3f p01 = spherePoint(theta0, phi1);
            TSVec3f p10 = spherePoint(theta1, phi0);
            TSVec3f p11 = spherePoint(theta1, phi1);

            // Tangent: derivative w.r.t theta
            TSVec3f t00 = normalize(TSVec3f(-std::sin(theta0) * std::sin(phi0), 0, std::cos(theta0) * std::sin(phi0)));
            TSVec3f t01 = normalize(TSVec3f(-std::sin(theta0) * std::sin(phi1), 0, std::cos(theta0) * std::sin(phi1)));
            TSVec3f t10 = normalize(TSVec3f(-std::sin(theta1) * std::sin(phi0), 0, std::cos(theta1) * std::sin(phi0)));
            TSVec3f t11 = normalize(TSVec3f(-std::sin(theta1) * std::sin(phi1), 0, std::cos(theta1) * std::sin(phi1)));

            auto V3 = [](float x, float y, float z) { return TSVec3f(x, y, z); };
            auto V2 = [](float x, float y) { return TSVec2f(x, y); };
            auto V2Zero = []() { return TSVec2f(0.0f, 0.0f); };

            uint32_t base = (uint32_t)model->vertices.size();

            model->vertices.push_back({p00, p00, t00, V3(1,1,1), V2(u0,v0), V2Zero(), V2Zero(), V2Zero()});
            model->vertices.push_back({p10, p10, t10, V3(1,1,1), V2(u1,v0), V2Zero(), V2Zero(), V2Zero()});
            model->vertices.push_back({p01, p01, t01, V3(1,1,1), V2(u0,v1), V2Zero(), V2Zero(), V2Zero()});
            model->vertices.push_back({p11, p11, t11, V3(1,1,1), V2(u1,v1), V2Zero(), V2Zero(), V2Zero()});

            model->indices.push_back(base);
            model->indices.push_back(base + 1);
            model->indices.push_back(base + 2);
            model->indices.push_back(base + 1);
            model->indices.push_back(base + 3);
            model->indices.push_back(base + 2);
        }
    }

    return model;
}

}
