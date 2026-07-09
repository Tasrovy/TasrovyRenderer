#pragma once
#include "TSVector.h"
#include "TSMatrix.h"
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>

namespace Tasrovy::FS {

struct PositionKey {
    double time;
    TSVec3f value;
};

struct RotationKey {
    double time;
    glm::quat value;
};

struct ScalingKey {
    double time;
    TSVec3f value;
};

struct BoneChannel {
    std::string boneName;
    std::vector<PositionKey> positionKeys;
    std::vector<RotationKey> rotationKeys;
    std::vector<ScalingKey> scalingKeys;
};

class Anim {
public:
    Anim() = default;
    ~Anim() = default;

    Anim(const Anim&) = delete;
    Anim& operator=(const Anim&) = delete;

    Anim(Anim&&) = default;
    Anim& operator=(Anim&&) = default;

    void SetDuration(double d) { duration = d; }
    void SetTicksPerSecond(double t) { ticksPerSecond = t; }
    void SetName(const std::string& n) { name = n; }
    void AddChannel(BoneChannel channel) { channels.push_back(std::move(channel)); }

    double GetDuration() const { return duration; }
    double GetTicksPerSecond() const { return ticksPerSecond; }
    const std::string& GetName() const { return name; }
    const std::vector<BoneChannel>& GetChannels() const { return channels; }
    std::vector<BoneChannel>& GetChannels() { return channels; }

    double GetDurationInSeconds() const {
        if (ticksPerSecond > 0.0) return duration / ticksPerSecond;
        return duration;
    }

private:
    std::string name;
    double duration = 0.0;
    double ticksPerSecond = 0.0;
    std::vector<BoneChannel> channels;
};

}
