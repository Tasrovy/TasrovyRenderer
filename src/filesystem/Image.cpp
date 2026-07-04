#include "Image.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Tasrovy {

Image::~Image() {
    Unload();
}

Image::Image(Image&& other) noexcept
    : width(other.width), height(other.height), channels(other.channels), data(other.data) {
    other.width = 0;
    other.height = 0;
    other.channels = 0;
    other.data = nullptr;
}

Image& Image::operator=(Image&& other) noexcept {
    if (this != &other) {
        Unload();
        width = other.width;
        height = other.height;
        channels = other.channels;
        data = other.data;
        other.width = 0;
        other.height = 0;
        other.channels = 0;
        other.data = nullptr;
    }
    return *this;
}

bool Image::LoadFromFile(const std::string& path, bool flipY) {
    Unload();
    stbi_set_flip_vertically_on_load(flipY);
    data = stbi_load(path.c_str(), &width, &height, &channels, 0);
    return data != nullptr;
}

bool Image::LoadFromMemory(const unsigned char* buffer, size_t length, bool flipY) {
    Unload();
    stbi_set_flip_vertically_on_load(flipY);
    data = stbi_load_from_memory(buffer, (int)length, &width, &height, &channels, 0);
    return data != nullptr;
}

void Image::Unload() {
    if (data) {
        stbi_image_free(data);
        data = nullptr;
    }
    width = 0;
    height = 0;
    channels = 0;
}

}
