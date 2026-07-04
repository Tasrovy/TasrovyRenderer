#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace Tasrovy {

class Image {
public:
    Image() = default;
    ~Image();

    Image(Image&& other) noexcept;
    Image& operator=(Image&& other) noexcept;

    Image(const Image&) = delete;
    Image& operator=(const Image&) = delete;

    bool LoadFromFile(const std::string& path, bool flipY = true);
    bool LoadFromMemory(const unsigned char* buffer, size_t length, bool flipY = true);
    void Unload();

    int GetWidth() const { return width; }
    int GetHeight() const { return height; }
    int GetChannels() const { return channels; }
    const unsigned char* GetData() const { return data; }
    unsigned char* GetData() { return data; }
    bool IsLoaded() const { return data != nullptr; }
    size_t GetDataSize() const { return (size_t)width * height * channels; }

private:
    int width = 0;
    int height = 0;
    int channels = 0;
    unsigned char* data = nullptr;
};

}
