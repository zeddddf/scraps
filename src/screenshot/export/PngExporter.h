#pragma once

#include <vector>
#include <cstddef>

class PngExporter {
public:
    static bool Encode(const std::vector<std::byte>& bgra8, int width, int height, std::vector<std::byte>& outPng);
};
