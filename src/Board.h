#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace chalk {
constexpr float CanvasWidth = 1200.0f;
constexpr float CanvasHeight = 720.0f;
constexpr size_t MaxStrokes = 20000;
constexpr size_t MaxPoints = 1000000;

struct Point { float x, y; };
struct Stroke {
    uint32_t color = 0xF4F1DE;
    float width = 4;
    bool eraser = false;
    std::vector<Point> points;
};
using Drawing = std::vector<Stroke>;

bool SaveDrawing(const std::filesystem::path& path, const Drawing& drawing, std::wstring& error);
bool LoadDrawing(const std::filesystem::path& path, Drawing& drawing, std::wstring& error);
size_t PointCount(const Drawing& drawing);
}
