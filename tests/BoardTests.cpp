#include "../src/Board.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <fstream>
#include <iostream>
#include <stdexcept>

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    const auto folder = std::filesystem::current_path() / L".build" / (L"ChalkTests_" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    try {
        const auto path = folder / L"저장 테스트.chalk";
        std::wstring error;
        chalk::Drawing original = {{0xFFE083, 7, false, {{0, 0}, {0.5f, 0.75f}, {1, 1}}},
                                   {0xFFFFFF, 28, true, {{0.2f, 0.2f}}}};
        if (!chalk::SaveDrawing(path, original, error)) { std::cerr << "Save failed; Windows error " << GetLastError() << '\n'; Check(false, "save Unicode path"); }
        chalk::Drawing loaded;
        Check(chalk::LoadDrawing(path, loaded, error), "load roundtrip");
        Check(loaded.size() == 2 && chalk::PointCount(loaded) == 4, "stroke and point counts");
        Check(loaded[0].points[1].x == 0.5f && loaded[0].points[1].y == 0.75f &&
              loaded[0].color == 0xFFE083 && loaded[0].width == 7 && loaded[1].eraser, "roundtrip properties");
        Check(chalk::SaveDrawing(path, {}, error) && chalk::LoadDrawing(path, loaded, error) && loaded.empty(), "overwrite and empty drawing");
        const char* invalid[] = {
            "BAD 1\n0\n", "CHALKBOARD 2\n0\n", "CHALKBOARD 1\n20001\n",
            "CHALKBOARD 1\n1\n16777216 4 0 1\n0 0\n",
            "CHALKBOARD 1\n1\n1 4 2 1\n0 0\n",
            "CHALKBOARD 1\n1\n1 4 0 0\n",
            "CHALKBOARD 1\n1\n1 4 0 1000001\n",
            "CHALKBOARD 1\n1\n1 4 0 1\n-0.1 0\n",
            "CHALKBOARD 1\n1\n1 4 0 1\n0 1.1\n",
            "CHALKBOARD 1\n1\n1 0 0 1\n0 0\n",
            "CHALKBOARD 1\n1\n1 4 0 2\n0 0\n",
            "CHALKBOARD 1\n0\nTRAILING DATA"
        };
        for (const char* text : invalid) {
            { std::ofstream output(path, std::ios::binary); output << text; }
            loaded = original;
            Check(!chalk::LoadDrawing(path, loaded, error) && !error.empty(), "reject invalid file");
            Check(loaded.size() == 2 && loaded[0].points.size() == 3, "failed load preserves current drawing");
        }
        Check(chalk::SaveDrawing(path, original, error), "restore valid drawing");
        auto invalidDrawing = original;
        invalidDrawing[0].width = 0;
        Check(!chalk::SaveDrawing(path, invalidDrawing, error), "reject invalid save");
        Check(chalk::LoadDrawing(path, loaded, error) && loaded.size() == 2, "failed save preserves previous file");
        Check(!chalk::LoadDrawing(folder / L"missing.chalk", loaded, error), "missing file");
        Check(!chalk::SaveDrawing(folder / L"missing" / L"file.chalk", original, error), "unwritable directory");
        std::cout << "PASS: roundtrip, Unicode path, empty drawing, overwrite, 12 malformed files, data preservation, missing paths\n";
        std::filesystem::remove_all(folder);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        std::filesystem::remove_all(folder);
        return 1;
    }
}
