#pragma once

#include <string>
#include <memory>

namespace Tasrovy::Render {

class Texture : public std::enable_shared_from_this<Texture> {
public:
    enum class Type { Texture2D, Cubemap };

    static std::shared_ptr<Texture> create();
    static std::shared_ptr<Texture> createFromFile(const std::string& path, bool generateMips = true);
    static std::shared_ptr<Texture> createCubemap(const std::string& directoryPath);

    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    void loadFromFile(const std::string& path, bool generateMips = true);
    void loadCubemap(const std::string& directoryPath);

    Type getType() const;
    bool hasMipmaps() const;
    const std::string& getFilePath() const;

private:
    Texture() = default;

    std::string filePath_;
    Type type_ = Type::Texture2D;
    bool generateMips_ = true;
};

} // namespace Tasrovy::Render
