#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <shellapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include "Board.h"

using Microsoft::WRL::ComPtr;
namespace {
constexpr COLORREF Background = RGB(246, 245, 240);
constexpr uint32_t BoardColor = 0x183F35;
constexpr std::array<uint32_t, 6> Colors = {0xF4F1DE, 0xFFE083, 0xF2A7B5, 0x99D9EA, 0xB6DDA0, 0xF4B183};
constexpr std::array<const wchar_t*, 6> ColorNames = {L"크림색 분필", L"노란색 분필", L"분홍색 분필", L"하늘색 분필", L"연두색 분필", L"주황색 분필"};
constexpr std::array<float, 3> Widths = {3, 7, 14};
enum { Save = 101, Open, Pen, Eraser, Undo, Clear, Help, Example, ColorFirst = 120, SizeFirst = 140 };

void HR(HRESULT result) {
    if (FAILED(result)) {
        char text[96];
        sprintf_s(text, "DirectX error 0x%08lX", static_cast<unsigned long>(result));
        throw std::runtime_error(text);
    }
}
D2D1_COLOR_F Color(uint32_t value, float alpha = 1) {
    return D2D1::ColorF(static_cast<float>((value >> 16) & 255) / 255,
                      static_cast<float>((value >> 8) & 255) / 255,
                      static_cast<float>(value & 255) / 255, alpha);
}
COLORREF GdiColor(uint32_t value) { return RGB((value >> 16) & 255, (value >> 8) & 255, value & 255); }
bool Inside(D2D1_RECT_F rect, D2D1_POINT_2F p) { return p.x >= rect.left && p.x <= rect.right && p.y >= rect.top && p.y <= rect.bottom; }

class App {
public:
    HWND window = nullptr;
    chalk::Drawing drawing;
    std::filesystem::path filePath;
    std::wstring status = L"새 칠판이 준비되었습니다";
    bool dirty = false;
    float dpiScale = 1;
    float width = 1280, height = 820;
    D2D1_RECT_F canvas{};
    HFONT buttonFont = nullptr;
    HWND tooltip = nullptr;
    std::vector<HWND> buttons;
    bool erasing = false, dragging = false, software = false;
    size_t colorIndex = 0, sizeIndex = 1, points = 0;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> immediate;
    ComPtr<IDXGISwapChain> swapChain;
    ComPtr<ID2D1Factory1> factory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> context;
    ComPtr<ID2D1Bitmap1> target;
    ComPtr<ID2D1SolidColorBrush> brush;
    ComPtr<ID2D1StrokeStyle> roundStroke;
    ComPtr<IDWriteFactory> writeFactory;
    ComPtr<IDWriteTextFormat> titleFormat, headingFormat, bodyFormat, smallFormat, emptyFormat, hintFormat;

    ~App() { if (buttonFont) DeleteObject(buttonFont); }
    void InitializeGraphics() {
        target.Reset(); brush.Reset(); context.Reset(); d2dDevice.Reset(); swapChain.Reset(); immediate.Reset(); device.Reset();
        D3D_FEATURE_LEVEL level{};
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
        HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 1, D3D11_SDK_VERSION, &device, &level, &immediate);
        software = FAILED(result);
        if (software) HR(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
            levels, 1, D3D11_SDK_VERSION, &device, &level, &immediate));
        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory> dxgiFactory;
        HR(device.As(&dxgiDevice));
        HR(dxgiDevice->GetAdapter(&adapter));
        HR(adapter->GetParent(IID_PPV_ARGS(&dxgiFactory)));
        DXGI_SWAP_CHAIN_DESC desc{};
        desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2;
        desc.OutputWindow = window;
        desc.Windowed = TRUE;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        HR(dxgiFactory->CreateSwapChain(device.Get(), &desc, &swapChain));
        HR(dxgiFactory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
        if (!factory) {
            D2D1_FACTORY_OPTIONS options{};
            HR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &options,
                reinterpret_cast<void**>(factory.GetAddressOf())));
            auto style = D2D1::StrokeStyleProperties();
            style.startCap = style.endCap = style.dashCap = D2D1_CAP_STYLE_ROUND;
            style.lineJoin = D2D1_LINE_JOIN_ROUND;
            HR(factory->CreateStrokeStyle(style, nullptr, 0, &roundStroke));
            HR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                reinterpret_cast<IUnknown**>(writeFactory.GetAddressOf())));
            MakeFormat(32, DWRITE_FONT_WEIGHT_BOLD, titleFormat);
            MakeFormat(19, DWRITE_FONT_WEIGHT_SEMI_BOLD, headingFormat);
            MakeFormat(14, DWRITE_FONT_WEIGHT_NORMAL, bodyFormat);
            MakeFormat(12, DWRITE_FONT_WEIGHT_NORMAL, smallFormat);
            MakeFormat(24, DWRITE_FONT_WEIGHT_NORMAL, emptyFormat);
            MakeFormat(14, DWRITE_FONT_WEIGHT_NORMAL, hintFormat);
            emptyFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            hintFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }
        HR(factory->CreateDevice(dxgiDevice.Get(), &d2dDevice));
        HR(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context));
        HR(context->CreateSolidColorBrush(Color(0), &brush));
        CreateTarget();
    }
    void MakeFormat(float size, DWRITE_FONT_WEIGHT weight, ComPtr<IDWriteTextFormat>& format) {
        HR(writeFactory->CreateTextFormat(L"맑은 고딕", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, size, L"ko-KR", &format));
        HR(format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));
    }
    void CreateTarget() {
        ComPtr<IDXGISurface> surface;
        HR(swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)));
        auto properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE), 96 * dpiScale, 96 * dpiScale);
        HR(context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target));
        context->SetTarget(target.Get());
        context->SetDpi(96 * dpiScale, 96 * dpiScale);
    }
    void Resize() {
        RECT rect{}; GetClientRect(window, &rect);
        dpiScale = GetDpiForWindow(window) / 96.0f;
        width = rect.right / dpiScale; height = rect.bottom / dpiScale;
        const float availableWidth = std::max(1.0f, width - 282);
        const float availableHeight = std::max(1.0f, height - 295);
        const float canvasWidth = std::min(availableWidth, availableHeight * chalk::CanvasWidth / chalk::CanvasHeight);
        const float canvasHeight = canvasWidth * chalk::CanvasHeight / chalk::CanvasWidth;
        const float x = 248 + (availableWidth - canvasWidth) / 2;
        const float y = 178 + (availableHeight - canvasHeight) / 2;
        canvas = D2D1::RectF(x, y, x + canvasWidth, y + canvasHeight);
        if (context && rect.right && rect.bottom) {
            context->SetTarget(nullptr); target.Reset();
            immediate->ClearState(); immediate->Flush();
            HRESULT result = swapChain->ResizeBuffers(0, rect.right, rect.bottom, DXGI_FORMAT_UNKNOWN, 0);
            if (result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) InitializeGraphics();
            else { HR(result); CreateTarget(); }
        }
        LayoutButtons();
        Redraw();
    }
    void CreateButtons() {
        InitCommonControls();
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, nullptr, nullptr);
        AddButton(Open, L"불러오기", L"칠판 파일 불러오기  Ctrl+O");
        AddButton(Save, L"저장", L"칠판 저장  Ctrl+S / 다른 이름으로 저장  Ctrl+Shift+S");
        AddButton(Example, L"예제 열기", L"동봉된 풍경 예제 열기  F2");
        AddButton(Pen, L"분필  B", L"분필로 그리기  B");
        AddButton(Eraser, L"지우개  E", L"드래그해서 지우기  E");
        AddButton(Undo, L"되돌리기", L"마지막 획 되돌리기  Ctrl+Z");
        AddButton(Clear, L"전체 지우기", L"칠판 전체 지우기  Ctrl+N");
        for (int i = 0; i < 6; ++i) AddButton(ColorFirst + i, ColorNames[i], ColorNames[i]);
        AddButton(SizeFirst, L"가늘게", L"가는 분필  3px");
        AddButton(SizeFirst + 1, L"보통", L"보통 분필  7px");
        AddButton(SizeFirst + 2, L"굵게", L"굵은 분필  14px");
        LayoutButtons();
    }
    void AddButton(int id, const wchar_t* label, const wchar_t* tip) {
        HWND button = CreateWindowExW(0, L"BUTTON", label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 0, 0, window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
        if (!button) throw std::runtime_error("Cannot create button");
        buttons.push_back(button);
        TOOLINFOW info{sizeof(info)};
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = window; info.uId = reinterpret_cast<UINT_PTR>(button); info.lpszText = const_cast<wchar_t*>(tip);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    void MoveButton(int id, float x, float y, float w, float h) {
        MoveWindow(GetDlgItem(window, id), static_cast<int>(x * dpiScale), static_cast<int>(y * dpiScale),
            static_cast<int>(w * dpiScale), static_cast<int>(h * dpiScale), TRUE);
    }
    void LayoutButtons() {
        if (buttons.empty()) return;
        if (buttonFont) DeleteObject(buttonFont);
        buttonFont = CreateFontW(-static_cast<int>(14 * dpiScale), 0, 0, 0, FW_MEDIUM, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"맑은 고딕");
        MoveButton(Open, width - 284, 37, 116, 42);
        MoveButton(Save, width - 152, 37, 116, 42);
        MoveButton(Example, width - 416, 37, 116, 42);
        MoveButton(Pen, 40, 184, 158, 42);
        MoveButton(Eraser, 40, 236, 158, 42);
        MoveButton(Undo, 40, 298, 158, 40);
        MoveButton(Clear, 40, 348, 158, 40);
        for (int i = 0; i < 6; ++i) MoveButton(ColorFirst + i, 40.0f + (i % 3) * 54, 438.0f + (i / 3) * 52, 44, 42);
        for (int i = 0; i < 3; ++i) MoveButton(SizeFirst + i, 40.0f + i * 54, 586, 44, 40);
        UpdateButtons();
    }
    void UpdateButtons() {
        if (buttons.empty()) return;
        EnableWindow(GetDlgItem(window, Undo), !drawing.empty());
        for (HWND button : buttons) InvalidateRect(button, nullptr, TRUE);
    }
    void DrawButton(const DRAWITEMSTRUCT& item) {
        const int id = static_cast<int>(item.CtlID);
        HDC dc = item.hDC;
        RECT r = item.rcItem;
        bool palette = id >= ColorFirst && id < ColorFirst + 6;
        bool selected = (id == Pen && !erasing) || (id == Eraser && erasing) ||
            (id >= SizeFirst && id < SizeFirst + 3 && static_cast<size_t>(id - SizeFirst) == sizeIndex);
        uint32_t fill = id == Save ? 0x245A49 : selected ? 0xE5EEE7 : 0xFFFFFF;
        if (item.itemState & ODS_SELECTED) fill = id == Save ? 0x183F35 : 0xD5E2D9;
        HBRUSH base = CreateSolidBrush(RGB(255, 255, 255));
        FillRect(dc, &r, base); DeleteObject(base);
        HBRUSH b = CreateSolidBrush(GdiColor(fill));
        HPEN p = CreatePen(PS_SOLID, std::max(1, static_cast<int>(dpiScale)), GdiColor(selected ? 0x6E9C81 : 0xE0E5DF));
        HGDIOBJ oldBrush = SelectObject(dc, b), oldPen = SelectObject(dc, p);
        RoundRect(dc, r.left, r.top, r.right - 1, r.bottom - 1, static_cast<int>(12 * dpiScale), static_cast<int>(12 * dpiScale));
        if (palette) {
            const bool active = static_cast<size_t>(id - ColorFirst) == colorIndex;
            const int inset = static_cast<int>(8 * dpiScale);
            HBRUSH swatch = CreateSolidBrush(GdiColor(Colors[id - ColorFirst]));
            HPEN edge = CreatePen(PS_SOLID, static_cast<int>((active ? 3 : 1) * dpiScale), GdiColor(active ? 0x245A49 : 0xB9C1B9));
            SelectObject(dc, swatch); SelectObject(dc, edge);
            Ellipse(dc, r.left + inset, r.top + inset, r.right - inset, r.bottom - inset);
            SelectObject(dc, p); SelectObject(dc, b); DeleteObject(swatch); DeleteObject(edge);
        } else {
            wchar_t label[96]{}; GetWindowTextW(item.hwndItem, label, 96);
            if (id >= SizeFirst && id < SizeFirst + 3) swprintf_s(label, L"%dpx", static_cast<int>(Widths[id - SizeFirst]));
            HGDIOBJ font = SelectObject(dc, buttonFont);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, GdiColor((item.itemState & ODS_DISABLED) ? 0xA8B0A8 : id == Save ? 0xFFFFFF : 0x2F4036));
            DrawTextW(dc, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(dc, font);
        }
        if (item.itemState & ODS_FOCUS) { RECT focus = r; InflateRect(&focus, -4, -4); DrawFocusRect(dc, &focus); }
        SelectObject(dc, oldBrush); SelectObject(dc, oldPen); DeleteObject(b); DeleteObject(p);
    }
    void Fill(D2D1_RECT_F rect, uint32_t color, float radius = 0) {
        brush->SetColor(Color(color));
        if (radius) context->FillRoundedRectangle(D2D1::RoundedRect(rect, radius, radius), brush.Get());
        else context->FillRectangle(rect, brush.Get());
    }
    void Text(const std::wstring& text, D2D1_RECT_F rect, IDWriteTextFormat* format, uint32_t color) {
        brush->SetColor(Color(color));
        context->DrawTextW(text.c_str(), static_cast<UINT32>(text.size()), format, rect, brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
    }
    void Render() {
        if (!context || IsIconic(window)) return;
        context->BeginDraw();
        context->SetTransform(D2D1::Matrix3x2F::Identity());
        context->Clear(Color(0xF6F5F0));
        Text(L"CHALK", {28, 22, 210, 70}, titleFormat.Get(), 0x244B3D);
        Text(L"생각을 그리는 나만의 칠판", {30, 76, 390, 103}, bodyFormat.Get(), 0x6F7A70);
        Fill({208, 35, 271, 60}, 0xE4EBE2, 10);
        Text(L"DX 11", {218, 39, 266, 60}, smallFormat.Get(), 0x496953);
        Fill({28, 114, width - 28, 115}, 0xDDDFD5);
        Fill({28, 138, 212, height - 58}, 0xFFFFFF, 14);
        Text(L"그리기 도구", {40, 150, 198, 177}, bodyFormat.Get(), 0x344C3E);
        Fill({40, 285, 198, 286}, 0xE6E9E2);
        Text(L"분필 색상", {40, 405, 198, 432}, bodyFormat.Get(), 0x344C3E);
        Text(L"굵기", {40, 553, 198, 578}, bodyFormat.Get(), 0x344C3E);
        Text(L"나의 칠판", {242, 132, width - 210, 163}, headingFormat.Get(), 0x2F483A);
        const std::wstring stateLabel = dirty ? L"●  저장하지 않음" : filePath.empty() ? L"새 문서" : L"✓  저장됨";
        Text(stateLabel, {width - 188, 141, width - 28, 165}, smallFormat.Get(), dirty ? 0x956C35 : 0x507A5B);
        Fill({canvas.left - 12, canvas.top - 9, canvas.right + 12, canvas.bottom + 17}, 0xD5D5C9, 9);
        Fill({canvas.left - 12, canvas.top - 12, canvas.right + 12, canvas.bottom + 12}, 0x9F8061, 9);
        Fill({canvas.left - 5, canvas.top - 5, canvas.right + 5, canvas.bottom + 5}, 0x644E38, 3);
        Fill(canvas, BoardColor);
        context->PushAxisAlignedClip(canvas, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        const float scale = (canvas.right - canvas.left) / chalk::CanvasWidth;
        const auto position = [this](chalk::Point p) {
            return D2D1::Point2F(canvas.left + p.x * (canvas.right - canvas.left), canvas.top + p.y * (canvas.bottom - canvas.top));
        };
        for (const auto& stroke : drawing) {
            brush->SetColor(Color(stroke.eraser ? BoardColor : stroke.color));
            const float lineWidth = stroke.width * scale;
            if (stroke.points.size() == 1) context->FillEllipse(D2D1::Ellipse(position(stroke.points[0]), lineWidth / 2, lineWidth / 2), brush.Get());
            for (size_t i = 1; i < stroke.points.size(); ++i)
                context->DrawLine(position(stroke.points[i - 1]), position(stroke.points[i]), brush.Get(), lineWidth, roundStroke.Get());
        }
        if (drawing.empty()) {
            const float middle = (canvas.top + canvas.bottom) / 2;
            Text(L"마우스로 생각을 그려보세요", {canvas.left, middle - 38, canvas.right, middle + 2}, emptyFormat.Get(), 0x86A594);
            Text(L"마우스를 누른 채 드래그하면 선이 그려집니다", {canvas.left, middle + 10, canvas.right, middle + 42}, hintFormat.Get(), 0x709480);
        }
        context->PopAxisAlignedClip();
        const float trayY = canvas.bottom + 12;
        Fill({canvas.right - 150, trayY, canvas.right - 14, trayY + 10}, 0x796046, 3);
        Fill({canvas.right - 135, trayY - 4, canvas.right - 100, trayY + 3}, Colors[colorIndex], 2);
        Fill({canvas.right - 89, trayY - 4, canvas.right - 54, trayY + 3}, 0xFFE083, 2);
        Text(std::to_wstring(drawing.size()) + L"개의 획", {canvas.left, canvas.bottom + 29, canvas.left + 130, canvas.bottom + 53}, smallFormat.Get(), 0x778173);
        const auto filename = filePath.empty() ? L"아직 저장하지 않은 칠판" : filePath.filename().wstring();
        Text(filename, {canvas.left + 133, canvas.bottom + 29, canvas.right, canvas.bottom + 53}, smallFormat.Get(), 0x778173);
        Fill({28, height - 44, width - 28, height - 43}, 0xDDDFD5);
        Text(L"Ctrl+S 저장    Ctrl+O 불러오기    Ctrl+Z 되돌리기    F1 도움말", {30, height - 32, width - 405, height - 9}, smallFormat.Get(), 0x6C796C);
        Text(status, {width - 395, height - 32, width - 28, height - 9}, smallFormat.Get(), 0x466C50);
        HRESULT result = context->EndDraw();
        if (SUCCEEDED(result)) result = swapChain->Present(1, 0);
        if (result == D2DERR_RECREATE_TARGET || result == DXGI_ERROR_DEVICE_REMOVED || result == DXGI_ERROR_DEVICE_RESET) {
            InitializeGraphics(); Redraw();
        } else HR(result);
    }
    void Redraw() { InvalidateRect(window, nullptr, FALSE); }
    void Changed() {
        dirty = true; UpdateButtons();
        SetWindowTextW(window, L"CHALK Studio • 저장하지 않은 변경사항");
        Redraw();
    }
    void FinishStroke() {
        dragging = false;
        if (GetCapture() == window) ReleaseCapture();
    }
    chalk::Point ToPoint(D2D1_POINT_2F p) const {
        return {std::clamp((p.x - canvas.left) / (canvas.right - canvas.left), 0.0f, 1.0f),
                std::clamp((p.y - canvas.top) / (canvas.bottom - canvas.top), 0.0f, 1.0f)};
    }
    void BeginStroke(D2D1_POINT_2F p) {
        if (!Inside(canvas, p)) return;
        if (drawing.size() >= chalk::MaxStrokes || points >= chalk::MaxPoints) {
            status = L"그림 한도에 도달했습니다. 저장 후 새 칠판을 사용하세요."; Redraw(); return;
        }
        SetFocus(window); SetCapture(window); dragging = true;
        drawing.push_back({Colors[colorIndex], erasing ? 36.0f : Widths[sizeIndex], erasing, {ToPoint(p)}});
        ++points;
        status = erasing ? L"지우개로 지우는 중" : L"분필로 그리는 중";
        Changed();
    }
    void MoveStroke(D2D1_POINT_2F p) {
        if (!dragging || drawing.empty()) return;
        if (points >= chalk::MaxPoints) { FinishStroke(); status = L"좌표 수 한도에 도달했습니다"; Redraw(); return; }
        const auto point = ToPoint(p);
        const auto previous = drawing.back().points.back();
        const float dx = (point.x - previous.x) * chalk::CanvasWidth;
        const float dy = (point.y - previous.y) * chalk::CanvasHeight;
        if (dx * dx + dy * dy >= 0.25f) { drawing.back().points.push_back(point); ++points; Redraw(); }
        if (!Inside(canvas, p)) FinishStroke();
    }
    bool ChooseFile(bool save, std::filesystem::path& path) {
        wchar_t buffer[32768]{};
        if (save) wcscpy_s(buffer, filePath.empty() ? L"나의 칠판.chalk" : filePath.c_str());
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window;
        dialog.lpstrFilter = L"CHALK 칠판 파일 (*.chalk)\0*.chalk\0\0";
        dialog.lpstrFile = buffer; dialog.nMaxFile = ARRAYSIZE(buffer);
        dialog.lpstrDefExt = L"chalk";
        dialog.lpstrTitle = save ? L"칠판 저장" : L"칠판 불러오기";
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog)) { path = buffer; return true; }
        const DWORD error = CommDlgExtendedError();
        if (error) MessageBoxW(window, (L"파일 선택 창을 열 수 없습니다. 오류 " + std::to_wstring(error)).c_str(), L"파일 선택 오류", MB_ICONERROR);
        return false;
    }
    bool SaveFile(bool saveAs = false) {
        FinishStroke();
        auto path = filePath;
        if ((path.empty() || saveAs) && !ChooseFile(true, path)) return false;
        std::wstring error;
        if (!chalk::SaveDrawing(path, drawing, error)) { MessageBoxW(window, error.c_str(), L"저장 실패", MB_OK | MB_ICONERROR); return false; }
        filePath = path; dirty = false; status = L"저장 완료 · 그림을 안전하게 보관했습니다";
        SetWindowTextW(window, L"CHALK Studio • DirectX 11 칠판"); Redraw(); return true;
    }
    bool ConfirmDiscard() {
        if (!dirty) return true;
        const int result = MessageBoxW(window, L"변경한 칠판을 저장할까요?\n\n예: 저장하고 계속\n아니요: 변경사항을 버리고 계속\n취소: 현재 칠판 유지", L"저장하지 않은 변경사항", MB_YESNOCANCEL | MB_ICONQUESTION);
        if (result == IDCANCEL) return false;
        return result == IDYES ? SaveFile() : true;
    }
    void OpenFile() {
        FinishStroke();
        std::filesystem::path path;
        if (!ChooseFile(false, path)) return;
        LoadFile(path);
    }
    void LoadFile(const std::filesystem::path& path) {
        if (!ConfirmDiscard()) return;
        chalk::Drawing loaded;
        std::wstring error;
        if (!chalk::LoadDrawing(path, loaded, error)) { MessageBoxW(window, error.c_str(), L"불러오기 실패", MB_OK | MB_ICONERROR); return; }
        drawing = std::move(loaded); filePath = path; points = chalk::PointCount(drawing);
        dirty = false; status = L"불러오기 완료 · 저장한 그림을 복원했습니다";
        SetWindowTextW(window, L"CHALK Studio • DirectX 11 칠판"); UpdateButtons(); Redraw();
    }
    void Command(int id) {
        FinishStroke();
        if (id == Save) { SaveFile((GetKeyState(VK_SHIFT) & 0x8000) != 0); return; }
        if (id == Open) { OpenFile(); return; }
        if (id == Example) {
            wchar_t executable[32768]{};
            GetModuleFileNameW(nullptr, executable, ARRAYSIZE(executable));
            LoadFile(std::filesystem::path(executable).parent_path().parent_path().parent_path() / L"samples" / L"풍경_예제.chalk");
            return;
        }
        if (id == Pen || id == Eraser) { erasing = id == Eraser; status = erasing ? L"지우개를 선택했습니다" : L"분필을 선택했습니다"; }
        if (id == Undo && !drawing.empty()) {
            points -= drawing.back().points.size(); drawing.pop_back(); status = L"마지막 획을 되돌렸습니다"; Changed();
        }
        if (id == Clear && !drawing.empty()) {
            if (MessageBoxW(window, L"칠판 전체를 지울까요?\n저장한 파일은 유지되며 현재 칠판만 지워집니다.\n이 작업은 되돌릴 수 없습니다.", L"칠판 전체 지우기", MB_YESNO | MB_DEFBUTTON2 | MB_ICONQUESTION) == IDYES) {
                drawing.clear(); points = 0; status = L"칠판을 모두 지웠습니다"; Changed();
            }
        }
        if (id >= ColorFirst && id < ColorFirst + 6) { colorIndex = id - ColorFirst; erasing = false; status = ColorNames[colorIndex]; }
        if (id >= SizeFirst && id < SizeFirst + 3) { sizeIndex = id - SizeFirst; status = L"분필 굵기를 변경했습니다"; }
        if (id == Help) MessageBoxW(window,
            L"CHALK Studio 사용법\n\n마우스를 누른 채 드래그: 그리기 / 지우기\nB: 분필    E: 지우개    1~6: 분필 색상\nCtrl+S: 저장    Ctrl+Shift+S: 다른 이름으로 저장\nCtrl+O: 불러오기    Ctrl+Z: 마지막 획 되돌리기\nCtrl+N: 전체 지우기    F2: 예제 열기\nTab / Shift+Tab: 버튼 이동\n\n저장 파일은 .chalk 형식입니다.\n지우개도 하나의 획이므로 되돌릴 수 있습니다.\nGPU를 사용할 수 없으면 DirectX 11 WARP로 실행합니다.",
            L"도움말", MB_OK | MB_ICONINFORMATION);
        UpdateButtons(); Redraw();
    }
    LRESULT Handle(UINT message, WPARAM w, LPARAM l) {
        switch (message) {
        case WM_SIZE: FinishStroke(); if (w != SIZE_MINIMIZED) Resize(); return 0;
        case WM_DPICHANGED: {
            FinishStroke(); const RECT* r = reinterpret_cast<RECT*>(l);
            SetWindowPos(window, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE); return 0;
        }
        case WM_GETMINMAXINFO: {
            auto info = reinterpret_cast<MINMAXINFO*>(l);
            const float scale = GetDpiForWindow(window) / 96.0f;
            info->ptMinTrackSize = {static_cast<LONG>(960 * scale), static_cast<LONG>(720 * scale)}; return 0;
        }
        case WM_PAINT: { PAINTSTRUCT paint{}; BeginPaint(window, &paint); Render(); EndPaint(window, &paint); return 0; }
        case WM_ERASEBKGND: return 1;
        case WM_DRAWITEM: DrawButton(*reinterpret_cast<DRAWITEMSTRUCT*>(l)); return TRUE;
        case WM_COMMAND: Command(LOWORD(w)); return 0;
        case WM_LBUTTONDOWN: BeginStroke({GET_X_LPARAM(l) / dpiScale, GET_Y_LPARAM(l) / dpiScale}); return 0;
        case WM_MOUSEMOVE: MoveStroke({GET_X_LPARAM(l) / dpiScale, GET_Y_LPARAM(l) / dpiScale}); return 0;
        case WM_LBUTTONUP:
            MoveStroke({GET_X_LPARAM(l) / dpiScale, GET_Y_LPARAM(l) / dpiScale});
            if (dragging) { FinishStroke(); status = erasing ? L"지우기를 마쳤습니다" : L"획을 그렸습니다 · Ctrl+S로 저장하세요"; Redraw(); }
            return 0;
        case WM_CAPTURECHANGED: dragging = false; return 0;
        case WM_CANCELMODE: FinishStroke(); return 0;
        case WM_SETCURSOR: {
            POINT p{}; GetCursorPos(&p); ScreenToClient(window, &p);
            if (LOWORD(l) == HTCLIENT && Inside(canvas, {p.x / dpiScale, p.y / dpiScale})) {
                SetCursor(LoadCursorW(nullptr, IDC_CROSS)); return TRUE;
            }
            break;
        }
        case WM_CLOSE: FinishStroke(); if (ConfirmDiscard()) DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
        return DefWindowProcW(window, message, w, l);
    }
    static LRESULT CALLBACK Procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        App* app = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
            app->window = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }
        if (!app) return DefWindowProcW(hwnd, message, w, l);
        try { return app->Handle(message, w, l); }
        catch (const std::exception& e) {
            MessageBoxA(hwnd, e.what(), "CHALK Studio error", MB_OK | MB_ICONERROR);
            if (app->dirty) app->SaveFile(true);
            PostQuitMessage(1); return 0;
        }
    }
};
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(initialized)) return 1;
    int exitCode = 1;
    try {
        App app;
        WNDCLASSEXW type{sizeof(type)};
        type.hInstance = instance; type.lpfnWndProc = App::Procedure; type.lpszClassName = L"ChalkStudioWindow";
        type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        if (!RegisterClassExW(&type)) throw std::runtime_error("Cannot register window class");
        RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        const float scale = GetDpiForSystem() / 96.0f;
        const int w = std::min(static_cast<int>(1300 * scale), static_cast<int>(work.right - work.left - 40));
        const int h = std::min(static_cast<int>(880 * scale), static_cast<int>(work.bottom - work.top - 40));
        if (!CreateWindowExW(WS_EX_CONTROLPARENT, type.lpszClassName, L"CHALK Studio • DirectX 11 칠판",
            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, work.left + (work.right - work.left - w) / 2,
            work.top + (work.bottom - work.top - h) / 2, w, h, nullptr, nullptr, instance, &app))
            throw std::runtime_error("Cannot create window");
        app.Resize(); app.InitializeGraphics(); app.CreateButtons();
        int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argv && argc > 1) {
            std::wstring error;
            if (chalk::LoadDrawing(argv[1], app.drawing, error)) {
                app.filePath = argv[1]; app.points = chalk::PointCount(app.drawing);
                app.status = L"불러오기 완료 · 저장한 그림을 복원했습니다";
            } else MessageBoxW(app.window, error.c_str(), L"불러오기 실패", MB_OK | MB_ICONERROR);
            app.UpdateButtons();
        }
        if (argv) LocalFree(argv);
        std::vector<ACCEL> accelerators = {
            {FVIRTKEY | FCONTROL, 'S', Save}, {FVIRTKEY | FCONTROL | FSHIFT, 'S', Save},
            {FVIRTKEY | FCONTROL, 'O', Open}, {FVIRTKEY | FCONTROL, 'Z', Undo}, {FVIRTKEY | FCONTROL, 'N', Clear},
            {FVIRTKEY, 'B', Pen}, {FVIRTKEY, 'E', Eraser}, {FVIRTKEY, VK_F1, Help}, {FVIRTKEY, VK_F2, Example}
        };
        for (int i = 0; i < 6; ++i) accelerators.push_back({FVIRTKEY, static_cast<WORD>('1' + i), static_cast<WORD>(ColorFirst + i)});
        HACCEL table = CreateAcceleratorTableW(accelerators.data(), static_cast<int>(accelerators.size()));
        ShowWindow(app.window, show); UpdateWindow(app.window);
        MSG message{};
        BOOL result;
        while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
            if (!TranslateAcceleratorW(app.window, table, &message) && !IsDialogMessageW(app.window, &message)) {
                TranslateMessage(&message); DispatchMessageW(&message);
            }
        }
        DestroyAcceleratorTable(table);
        exitCode = result == -1 ? 1 : static_cast<int>(message.wParam);
    } catch (const std::exception& e) { MessageBoxA(nullptr, e.what(), "CHALK Studio initialization error", MB_OK | MB_ICONERROR); }
    CoUninitialize();
    return exitCode;
}
