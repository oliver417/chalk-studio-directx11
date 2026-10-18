#include "Board.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace chalk {
namespace {
constexpr uintmax_t MaxFileBytes = 32 * 1024 * 1024;
bool ValidStroke(const Stroke& stroke) {
    if (stroke.color > 0xFFFFFF || !std::isfinite(stroke.width) ||
        stroke.width < 1 || stroke.width > 80 || stroke.points.empty()) return false;
    for (const auto& p : stroke.points)
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || p.x < 0 || p.x > 1 || p.y < 0 || p.y > 1) return false;
    return true;
}
}

size_t PointCount(const Drawing& drawing) {
    size_t count = 0;
    for (const auto& stroke : drawing) count += stroke.points.size();
    return count;
}

bool SaveDrawing(const std::filesystem::path& path, const Drawing& drawing, std::wstring& error) {
    error.clear();
    if (drawing.size() > MaxStrokes || PointCount(drawing) > MaxPoints) {
        error = L"그림 데이터가 저장 한도를 넘었습니다."; return false;
    }
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(std::numeric_limits<float>::max_digits10);
    stream << "CHALKBOARD 1\n" << drawing.size() << '\n';
    for (const auto& stroke : drawing) {
        if (!ValidStroke(stroke)) { error = L"저장할 그림 데이터가 올바르지 않습니다."; return false; }
        stream << stroke.color << ' ' << stroke.width << ' ' << stroke.eraser << ' ' << stroke.points.size() << '\n';
        for (const auto& p : stroke.points) stream << p.x << ' ' << p.y << '\n';
    }
    const auto text = stream.str();
    if (text.size() > MaxFileBytes) { error = L"저장 파일은 32MB 이하여야 합니다."; return false; }
    auto temp = path;
    temp += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
    HANDLE file = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { error = L"파일을 만들 수 없습니다. 저장 위치와 쓰기 권한을 확인하세요. Windows 오류 " + std::to_wstring(GetLastError()); return false; }
    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    if (ok) ok = FlushFileBuffers(file) != FALSE;
    DWORD code = ok ? ERROR_SUCCESS : GetLastError();
    if (!CloseHandle(file)) { ok = false; code = GetLastError(); }
    // Write to a sibling file first so a failed save leaves the previous drawing intact.
    if (ok) { ok = MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE; if (!ok) code = GetLastError(); }
    if (!ok) { DeleteFileW(temp.c_str()); error = L"저장하지 못했습니다. 디스크 공간과 파일 권한을 확인하세요. Windows 오류 " + std::to_wstring(code); }
    return ok;
}

bool LoadDrawing(const std::filesystem::path& path, Drawing& drawing, std::wstring& error) {
    error.clear();
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes > MaxFileBytes) { error = L"파일이 없거나 32MB 제한을 넘었습니다."; return false; }
    std::ifstream stream(path, std::ios::binary);
    stream.imbue(std::locale::classic());
    std::string magic;
    int version = 0;
    size_t count = 0;
    if (!(stream >> magic >> version >> count) || magic != "CHALKBOARD" || version != 1 || count > MaxStrokes) {
        error = L"지원하는 칠판 파일이 아닙니다. CHALKBOARD 버전 1 파일을 선택하세요."; return false;
    }
    Drawing loaded;
    loaded.reserve(count);
    size_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        Stroke stroke;
        int eraser = 0;
        size_t points = 0;
        if (!(stream >> stroke.color >> stroke.width >> eraser >> points) ||
            (eraser != 0 && eraser != 1) || points == 0 || points > MaxPoints - total) {
            error = L"획 데이터가 손상되었거나 좌표 수 제한을 넘었습니다."; return false;
        }
        stroke.eraser = eraser == 1;
        stroke.points.resize(points);
        for (auto& p : stroke.points) {
            if (!(stream >> p.x >> p.y)) { error = L"파일의 좌표 데이터가 손상되었습니다."; return false; }
        }
        if (!ValidStroke(stroke)) { error = L"파일에 허용 범위를 벗어난 좌표·색·굵기가 있습니다."; return false; }
        total += points;
        loaded.push_back(std::move(stroke));
    }
    stream >> std::ws;
    if (stream.bad() || !stream.eof()) { error = L"파일 끝에 잘못된 데이터가 있습니다."; return false; }
    // Commit only after the entire file has been validated.
    drawing.swap(loaded);
    return true;
}
}
