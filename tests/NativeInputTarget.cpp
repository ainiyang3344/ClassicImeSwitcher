#include <Windows.h>

namespace {
HWND edit = nullptr;
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            20, 35, 450, 35, window, nullptr, GetModuleHandleW(nullptr), nullptr);
        SendMessageW(edit, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return 0;
    case WM_SETFOCUS: SetFocus(edit); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: return DefWindowProcW(window, message, wParam, lParam);
    }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int)
{
    constexpr auto className = L"LYLab.ClassicIme.NativeInputTarget";
    WNDCLASSW type{};
    type.hInstance = instance; type.lpfnWndProc = windowProc; type.lpszClassName = className;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    if (!RegisterClassW(&type)) return 1;
    const HWND window = CreateWindowW(className, L"Classic IME native cross-process verification", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 180, nullptr, nullptr, instance, nullptr);
    if (!window || !edit) return 2;
    ShowWindow(window, SW_SHOWNORMAL); UpdateWindow(window); SetFocus(edit);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
