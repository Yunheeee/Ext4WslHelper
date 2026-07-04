#include "PasswordDialog.h"
#include <vector>

// ─── sudo 密码输入对话框 ──────────────────────────────────────────────────

struct PwdDlgContext {
    wchar_t* result;
};

static LRESULT CALLBACK PasswordDialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        HWND parent = GetParent(hwnd);
        RECT pr, dr;
        GetWindowRect(parent ? parent : GetDesktopWindow(), &pr);
        GetWindowRect(hwnd, &dr);
        SetWindowPos(hwnd, nullptr,
                     pr.left + (pr.right - pr.left - (dr.right - dr.left)) / 2,
                     pr.top + (pr.bottom - pr.top - (dr.bottom - dr.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetFocus(GetDlgItem(hwnd, 1002));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == 1001) {
            PwdDlgContext* ctx = (PwdDlgContext*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (ctx) {
                wchar_t buf[256] = {};
                GetDlgItemTextW(hwnd, 1002, buf, 256);
                ctx->result = (wchar_t*)HeapAlloc(GetProcessHeap(),
                                                   HEAP_ZERO_MEMORY,
                                                   256 * sizeof(wchar_t));
                if (ctx->result) wcscpy_s(ctx->result, 256, buf);
                SecureZeroMemory(buf, sizeof(buf));
            }
            DestroyWindow(hwnd);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

std::string ShowPasswordDialog(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = PasswordDialogProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Ext4PasswordDlg";
        RegisterClassExW(&wc);
        registered = true;
    }

    PwdDlgContext ctx = { nullptr };

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
                               L"Ext4PasswordDlg", L"需要管理员权限",
                               WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_POPUP,
                               0, 0, 350, 145,
                               parent, nullptr, GetModuleHandleW(nullptr), &ctx);
    if (!dlg) return "";

    CreateWindowExW(0, L"STATIC", L"请输入 WSL sudo 密码:",
                    WS_CHILD | WS_VISIBLE, 15, 12, 320, 18,
                    dlg, nullptr, nullptr, nullptr);

    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
                    15, 34, 320, 24,
                    dlg, (HMENU)1002, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"确认",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    185, 72, 70, 26,
                    dlg, (HMENU)1001, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"取消",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                    265, 72, 70, 26,
                    dlg, (HMENU)IDCANCEL, nullptr, nullptr);

    EnableWindow(parent, FALSE);
    {
        MSG msg = {};
        while (IsWindow(dlg) && GetMessage(&msg, nullptr, 0, 0)) {
            if (!IsWindow(dlg)) break;
            if (!IsDialogMessage(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    std::string password;
    if (ctx.result) {
        int len = WideCharToMultiByte(CP_UTF8, 0, ctx.result, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::vector<char> mb(len);
            WideCharToMultiByte(CP_UTF8, 0, ctx.result, -1, mb.data(), len, nullptr, nullptr);
            password = mb.data();
        }
        SecureZeroMemory(ctx.result, 256 * sizeof(wchar_t));
        HeapFree(GetProcessHeap(), 0, ctx.result);
        ctx.result = nullptr;
    }

    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return password;
}

// ─── WSL 安装用户创建对话框（用户名 + 密码 + 确认密码）────────────────────

struct WslUserCtx {
    wchar_t username[256];
    wchar_t password[256];
    bool confirmed;
};

static LRESULT CALLBACK WslUserSetupProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* cs = (CREATESTRUCT*)lParam;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        HWND parent = GetParent(hwnd);
        RECT pr, dr;
        GetWindowRect(parent ? parent : GetDesktopWindow(), &pr);
        GetWindowRect(hwnd, &dr);
        SetWindowPos(hwnd, nullptr,
                     pr.left + (pr.right - pr.left - (dr.right - dr.left)) / 2,
                     pr.top + (pr.bottom - pr.top - (dr.bottom - dr.top)) / 2,
                     0, 0, SWP_NOSIZE | SWP_NOZORDER);
        SetFocus(GetDlgItem(hwnd, 2001));
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == 2003) {
            WslUserCtx* ctx = (WslUserCtx*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
            if (ctx) {
                GetDlgItemTextW(hwnd, 2001, ctx->username, 256);
                GetDlgItemTextW(hwnd, 2002, ctx->password, 256);

                if (wcslen(ctx->username) == 0) {
                    MessageBoxW(hwnd, L"请输入用户名", L"提示", MB_OK | MB_ICONINFORMATION);
                    SetFocus(GetDlgItem(hwnd, 2001));
                    return TRUE;
                }
                if (wcslen(ctx->password) == 0) {
                    MessageBoxW(hwnd, L"请输入密码", L"提示", MB_OK | MB_ICONINFORMATION);
                    SetFocus(GetDlgItem(hwnd, 2002));
                    return TRUE;
                }
                ctx->confirmed = true;
            }
            DestroyWindow(hwnd);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            DestroyWindow(hwnd);
            return TRUE;
        }
        return FALSE;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return TRUE;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

bool ShowWslUserSetupDialog(HWND parent,
                             std::string& outUsername,
                             std::string& outPassword) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WslUserSetupProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"Ext4WslUserSetupDlg";
        RegisterClassExW(&wc);
        registered = true;
    }

    WslUserCtx ctx = {};
    ctx.confirmed = false;

    HWND dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
                               L"Ext4WslUserSetupDlg", L"WSL 用户设置",
                               WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_POPUP,
                               0, 0, 380, 190,
                               parent, nullptr, GetModuleHandleW(nullptr), &ctx);
    if (!dlg) return false;

    CreateWindowExW(0, L"STATIC", L"WSL Ubuntu 首次运行需要创建用户。\n请设置用户名和密码：",
                    WS_CHILD | WS_VISIBLE, 15, 10, 350, 30,
                    dlg, nullptr, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"用户名:",
                    WS_CHILD | WS_VISIBLE, 15, 48, 60, 18,
                    dlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                    80, 45, 280, 24,
                    dlg, (HMENU)2001, nullptr, nullptr);

    CreateWindowExW(0, L"STATIC", L"密  码:",
                    WS_CHILD | WS_VISIBLE, 15, 78, 60, 18,
                    dlg, nullptr, nullptr, nullptr);
    CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                    WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL | WS_TABSTOP,
                    80, 75, 280, 24,
                    dlg, (HMENU)2002, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"确认",
                    WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
                    210, 115, 70, 26,
                    dlg, (HMENU)2003, nullptr, nullptr);

    CreateWindowExW(0, L"BUTTON", L"取消",
                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                    290, 115, 70, 26,
                    dlg, (HMENU)IDCANCEL, nullptr, nullptr);

    EnableWindow(parent, FALSE);
    {
        MSG msg = {};
        while (IsWindow(dlg) && GetMessage(&msg, nullptr, 0, 0)) {
            if (!IsWindow(dlg)) break;
            if (!IsDialogMessage(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    bool result = false;
    if (ctx.confirmed) {
        int len;
        len = WideCharToMultiByte(CP_UTF8, 0, ctx.username, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::vector<char> mb(len);
            WideCharToMultiByte(CP_UTF8, 0, ctx.username, -1, mb.data(), len, nullptr, nullptr);
            outUsername = mb.data();
        }
        len = WideCharToMultiByte(CP_UTF8, 0, ctx.password, -1, nullptr, 0, nullptr, nullptr);
        if (len > 0) {
            std::vector<char> mb(len);
            WideCharToMultiByte(CP_UTF8, 0, ctx.password, -1, mb.data(), len, nullptr, nullptr);
            outPassword = mb.data();
        }
        SecureZeroMemory(ctx.username, sizeof(ctx.username));
        SecureZeroMemory(ctx.password, sizeof(ctx.password));
        result = true;
    }

    if (parent) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }

    return result;
}
