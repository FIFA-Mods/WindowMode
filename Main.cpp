#include "plugin.h"

using namespace plugin;

AddrType OrigCreateWindow = 0;
AddrType OrigWndProc = 0;
AddrType OrigRegisterClass = 0;
AddrType OrigAdjustWindowRect = 0;
AddrType OrigSetWindowLong = 0;
AddrType OrigMoveWindow = 0;
AddrType OrigSetCursorPos = 0;

bool IsWindowed() {
    static bool windowed = false;
    static bool initialized = false;
    if (!initialized) {
        void *vars = CallAndReturn<void *, 0x70606B>();
        windowed = CallMethodAndReturn<int, 0x706219>(vars, "WINDOWED", 1) != 0;
        initialized = true;
    }
    return windowed;
}

bool IsBorderless() {
    static bool borderless = false;
    static bool initialized = false;
    if (!initialized) {
        void *vars = CallAndReturn<void *, 0x70606B>();
        borderless = CallMethodAndReturn<int, 0x706219>(vars, "BORDERLESS", 1) != 0;
        initialized = true;
    }
    return borderless;
}

LONG GetWindowStyle(LONG style) {
    if (IsWindowed() && IsBorderless())
        return WS_VISIBLE | WS_MINIMIZEBOX | WS_POPUP;
    return style;
}

LONG GetWindowClassStyle(LONG style) {
    if (IsWindowed() && IsBorderless())
        return CS_CLASSDC | CS_DBLCLKS;
    return style;
}

void EnableDPIAwareness() {
    HMODULE hShcore = LoadLibraryA("Shcore.dll");
    if (hShcore) {
        typedef HRESULT(WINAPI *SetDpiAwarenessFunc)(int);
        SetDpiAwarenessFunc SetProcessDpiAwareness = (SetDpiAwarenessFunc)GetProcAddress(hShcore, "SetProcessDpiAwareness");
        if (SetProcessDpiAwareness)
            SetProcessDpiAwareness(1);
        FreeLibrary(hShcore);
    }
    else {
        HMODULE hUser32 = LoadLibraryA("User32.dll");
        if (hUser32) {
            typedef BOOL(WINAPI *SetDPIAwareFunc)();
            SetDPIAwareFunc SetProcessDPIAware = (SetDPIAwareFunc)GetProcAddress(hUser32, "SetProcessDPIAware");
            if (SetProcessDPIAware)
                SetProcessDPIAware();
            FreeLibrary(hUser32);
        }
    }
}

struct WindowRect {
    int X, Y, Width, Height;
};

WindowRect &InitialWindowRect() {
    static WindowRect initialWindowRect;
    return initialWindowRect;
}

WindowRect CalcWindowRect(int X, int Y, int Width, int Height, bool StoreInitialRect) {
    WindowRect rect;
    rect.X = X;
    rect.Y = Y;
    rect.Width = Width;
    rect.Height = Height;
    auto screenW = GetSystemMetrics(SM_CXMAXIMIZED);
    auto screenH = GetSystemMetrics(SM_CYMAXIMIZED);
    if (rect.Width < (screenW - 100) && rect.Height < (screenH - 100)) {
        rect.X = screenW / 2 - rect.Width / 2;
        rect.Y = screenH / 2 - rect.Height / 2;
    }
    else {
        rect.X = 0;
        rect.Y = 0;
        rect.Height -= 1;
    }
    if (StoreInitialRect)
        InitialWindowRect() = rect;
    return rect;
}

int WINAPI MyWndProc(HWND hWnd, UINT uCmd, WPARAM wParam, LPARAM lParam) {
    if (!OrigWndProc)
        return 0;
    if (IsWindowed() && IsBorderless()) {
        static POINT ptStart;
        static POINT ptOffset;
        static BOOL bDragging = FALSE;
        static const LONG BORDERLESS_MOVEBAR_HEIGHT = 20;
        if (uCmd == WM_LBUTTONDOWN) {
            if (!bDragging) {
                GetCursorPos(&ptStart);
                RECT rect;
                GetWindowRect(hWnd, &rect);
                ptOffset.x = ptStart.x - rect.left;
                ptOffset.y = ptStart.y - rect.top;
                if (ptStart.y <= (rect.top + BORDERLESS_MOVEBAR_HEIGHT)) {
                    bDragging = TRUE;
                    SetCapture(hWnd);
                }
            }
        }
        else if (uCmd == WM_MOUSEMOVE) {
            if (bDragging) {
                POINT pt;
                GetCursorPos(&pt);
                int dx = pt.x - ptStart.x;
                int dy = pt.y - ptStart.y;
                RECT rect;
                GetWindowRect(hWnd, &rect);
                MoveWindow(hWnd, rect.left + dx, rect.top + dy, rect.right - rect.left, rect.bottom - rect.top, TRUE);
                ptStart = pt;
            }
        }
        else if (uCmd == WM_LBUTTONUP) {
            bDragging = FALSE;
            ReleaseCapture();
        }
        else if (uCmd == WM_LBUTTONDBLCLK) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hWnd, &pt);
            RECT rect;
            GetClientRect(hWnd, &rect);
            if (pt.y <= BORDERLESS_MOVEBAR_HEIGHT) {
                MoveWindow(hWnd, InitialWindowRect().X, InitialWindowRect().Y,
                    InitialWindowRect().Width, InitialWindowRect().Height, TRUE);
            }
        }
    }
    return CallWindowProc((WNDPROC)OrigWndProc, hWnd, uCmd, wParam, lParam);
}

HWND WINAPI MyCreateWindowExA(DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle,
    int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam)
{
    if (OrigCreateWindow) {
        HWND h = 0;
        if (IsWindowed() && IsBorderless()) {
            EnableDPIAwareness();
            auto rect = CalcWindowRect(X, Y, nWidth, nHeight, false);
            return CallWinApiAndReturnDynGlobal<HWND>(OrigCreateWindow, dwExStyle, lpClassName, lpWindowName,
                GetWindowStyle(dwStyle), rect.X, rect.Y, rect.Width, rect.Height, hWndParent, hMenu, hInstance, lpParam);
        }
        else {
            return CallWinApiAndReturnDynGlobal<HWND>(OrigCreateWindow, dwExStyle, lpClassName, lpWindowName,
                dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam);
        }
    }
    return NULL;
}

BOOL WINAPI MyAdjustWindowRect(LPRECT lpRect, DWORD dwStyle, BOOL bMenu) {
    return OrigAdjustWindowRect ? CallWinApiAndReturnDynGlobal<BOOL>(OrigAdjustWindowRect, lpRect, GetWindowStyle(dwStyle), bMenu) : FALSE;
}

BOOL WINAPI MySetWindowLong(HWND hWnd, int nIndex, DWORD dwNewLong) {
    return OrigSetWindowLong ? CallWinApiAndReturnDynGlobal<BOOL>(OrigSetWindowLong, hWnd, nIndex, GetWindowStyle(dwNewLong)) : FALSE;
}

BOOL WINAPI MyMoveWindow(HWND hWnd, int X, int Y, int Width, int Height, BOOL bRepaint) {
    if (OrigMoveWindow) {
        if (IsWindowed() && IsBorderless()) {
            auto rect = CalcWindowRect(X, Y, Width, Height, true);
            return CallWinApiAndReturnDynGlobal<BOOL>(OrigMoveWindow, hWnd, rect.X, rect.Y, rect.Width, rect.Height, bRepaint);
        }
        else
            return CallWinApiAndReturnDynGlobal<BOOL>(OrigMoveWindow, hWnd, X, Y, Width, Height, bRepaint);
    }
    return FALSE;
}

ATOM WINAPI MyRegisterClassA(WNDCLASSA *lpWndClass) {
    if (OrigRegisterClass) {
        if (IsWindowed() && IsBorderless())
            lpWndClass->style = GetWindowClassStyle(lpWndClass->style);
        return CallWinApiAndReturnDynGlobal<ATOM>(OrigRegisterClass, lpWndClass);
    }
    return 0;
}

BOOL WINAPI MySetCursorPos(int X, int Y) {
    if (OrigSetCursorPos) {
        if (IsWindowed() && IsBorderless())
            return TRUE;
        return CallWinApiAndReturnDynGlobal<BOOL>(OrigSetCursorPos, X, Y);
    }
    return FALSE;
}

int METHOD GetNoLossFocusVariable(void *, DUMMY_ARG, char const *, int) {
    return IsWindowed() ? 1 : 0;
}

class WindowMode {
public:
    WindowMode() {
        if (!CheckPluginName(Obfuscate("WindowMode.asi")))
            return;
        auto v = FIFA::GetAppVersion();
        if (v.id() == ID_FIFA07_1100_RLD) {
            OrigCreateWindow = patch::RedirectCall(0x5E5D79, MyCreateWindowExA);
            OrigWndProc = patch::SetPointer(0x5E5CB8 + 4, MyWndProc);
            OrigRegisterClass = patch::RedirectCall(0x5E5D0A, MyRegisterClassA);
            OrigAdjustWindowRect = patch::RedirectCall(0x5E5D39, MyAdjustWindowRect);
            patch::RedirectCall(0x5E58E2, MyAdjustWindowRect);
            OrigSetWindowLong = patch::RedirectCall(0x5E58D4, MySetWindowLong);
            OrigMoveWindow = patch::RedirectCall(0x5E590A, MyMoveWindow);
            OrigSetCursorPos = patch::SetPointer(0x848488, MySetCursorPos);
            patch::RedirectCall(0x5F1751, GetNoLossFocusVariable);
        }
    }
} windowMode;
