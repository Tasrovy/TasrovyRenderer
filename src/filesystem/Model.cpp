#include "Model.hpp"
#include <glm/gtc/constants.hpp>
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
    model->vertices = {
        // Front (+Z)
        { {-h, -h,  h}, { 0, 0, 1}, {1, 0, 0}, {1,1,1}, {0,0}, {}, {}, {} },
        { { h, -h,  h}, { 0, 0, 1}, {1, 0, 0}, {1,1,1}, {1,0}, {}, {}, {} },
        { { h,  h,  h}, { 0, 0, 1}, {1, 0, 0}, {1,1,1}, {1,1}, {}, {}, {} },
        { {-h,  h,  h}, { 0, 0, 1}, {1, 0, 0}, {1,1,1}, {0,1}, {}, {}, {} },
        // Back (-Z)
        { { h, -h, -h}, { 0, 0,-1}, {-1, 0, 0}, {1,1,1}, {0,0}, {}, {}, {} },
        { {-h, -h, -h}, { 0, 0,-1}, {-1, 0, 0}, {1,1,1}, {1,0}, {}, {}, {} },
        { {-h,  h, -h}, { 0, 0,-1}, {-1, 0, 0}, {1,1,1}, {1,1}, {}, {}, {} },
        { { h,  h, -h}, { 0, 0,-1}, {-1, 0, 0}, {1,1,1}, {0,1}, {}, {}, {} },
        // Right (+X)
        { { h, -h,  h}, { 1, 0, 0}, {0, 0, 1}, {1,1,1}, {0,0}, {}, {}, {} },
        { { h, -h, -h}, { 1, 0, 0}, {0, 0, 1}, {1,1,1}, {1,0}, {}, {}, {} },
        { { h,  h, -h}, { 1, 0, 0}, {0, 0, 1}, {1,1,1}, {1,1}, {}, {}, {} },
        { { h,  h,  h}, { 1, 0, 0}, {0, 0, 1}, {1,1,1}, {0,1}, {}, {}, {} },
        // Left (-X)
        { {-h, -h, -h}, {-1, 0, 0}, { 0, 0,-1}, {1,1,1}, {0,0}, {}, {}, {} },
        { {-h, -h,  h}, {-1, 0, 0}, { 0, 0,-1}, {1,1,1}, {1,0}, {}, {}, {} },
        { {-h,  h,  h}, {-1, 0, 0}, { 0, 0,-1}, {1,1,1}, {1,1}, {}, {}, {} },
        { {-h,  h, -h}, {-1, 0, 0}, { 0, 0,-1}, {1,1,1}, {0,1}, {}, {}, {} },
        // Top (+Y)
        { {-h,  h,  h}, { 0, 1, 0}, {1, 0, 0}, {1,1,1}, {0,0}, {}, {}, {} },
        { { h,  h,  h}, { 0, 1, 0}, {1, 0, 0}, {1,1,1}, {1,0}, {}, {}, {} },
        { { h,  h, -h}, { 0, 1, 0}, {1, 0, 0}, {1,1,1}, {1,1}, {}, {}, {} },
        { {-h,  h, -h}, { 0, 1, 0}, {1, 0, 0}, {1,1,1}, {0,1}, {}, {}, {} },
        // Bottom (-Y)
        { {-h, -h, -h}, { 0,-1, 0}, {1, 0, 0}, {1,1,1}, {0,0}, {}, {}, {} },
        { { h, -h, -h}, { 0,-1, 0}, {1, 0, 0}, {1,1,1}, {1,0}, {}, {}, {} },
        { { h, -h,  h}, { 0,-1, 0}, {1, 0, 0}, {1,1,1}, {1,1}, {}, {}, {} },
        { {-h, -h,  h}, { 0,-1, 0}, {1, 0, 0}, {1,1,1}, {0,1}, {}, {}, {} },
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

            float theta0 = u0 * glm::two_pi<float>();
            float theta1 = u1 * glm::two_pi<float>();
            float phi0 = v0 * glm::pi<float>();
            float phi1 = v1 * glm::pi<float>();

            auto spherePoint = [](float theta, float phi) -> glm::vec3 {
                return {
                    std::sin(phi) * std::cos(theta),
                    std::cos(phi),
                    std::sin(phi) * std::sin(theta)
                };
            };

            glm::vec3 p00 = spherePoint(theta0, phi0);
            glm::vec3 p01 = spherePoint(theta0, phi1);
            glm::vec3 p10 = spherePoint(theta1, phi0);
            glm::vec3 p11 = spherePoint(theta1, phi1);

            // Tangent: derivative w.r.t theta
            glm::vec3 t00 = glm::normalize(glm::vec3(-std::sin(theta0) * std::sin(phi0), 0, std::cos(theta0) * std::sin(phi0)));
            glm::vec3 t01 = glm::normalize(glm::vec3(-std::sin(theta0) * std::sin(phi1), 0, std::cos(theta0) * std::sin(phi1)));
            glm::vec3 t10 = glm::normalize(glm::vec3(-std::sin(theta1) * std::sin(phi0), 0, std::cos(theta1) * std::sin(phi0)));
            glm::vec3 t11 = glm::normalize(glm::vec3(-std::sin(theta1) * std::sin(phi1), 0, std::cos(theta1) * std::sin(phi1)));

            uint32_t base = (uint32_t)model->vertices.size();

            model->vertices.push_back({p00, p00, t00, {1,1,1}, {u0, v0}, {}, {}, {}});
            model->vertices.push_back({p10, p10, t10, {1,1,1}, {u1, v0}, {}, {}, {}});
            model->vertices.push_back({p01, p01, t01, {1,1,1}, {u0, v1}, {}, {}, {}});
            model->vertices.push_back({p11, p11, t11, {1,1,1}, {u1, v1}, {}, {}, {}});

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
