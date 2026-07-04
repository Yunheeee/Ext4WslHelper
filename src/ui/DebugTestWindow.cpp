#include "DebugTestWindow.h"

#ifdef _DEBUG

#include "../command/PowershellExecutor.h"
#include "../command/WslExecutor.h"
#include "../command/CommandWindow.h"
#include "../common/Utils.h"
#include <string>
#include <vector>

// 清理管道捕获的交互式输出：去掉 \r 进度条叠加（只保留 \r\n 换行）
static std::string CleanCapturedOutput(const std::string& utf8) {
    if (utf8.empty()) return {};
    std::string result;
    result.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); i++) {
        if (utf8[i] == '\r') {
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n') {
                result += '\r';
                result += '\n';
                i++;
                continue;
            }
            continue;
        }
        result += utf8[i];
    }
    return result;
}

// 将 UTF-8 输出设置到 Edit 控件（含 \r 清理和 \r\n 转换）
static void SetOutputText(HWND hEdit, const std::string& utf8) {
    if (utf8.empty()) { SetWindowTextW(hEdit, L""); return; }
    std::string cleaned = CleanCapturedOutput(utf8);
    std::string converted;
    converted.reserve(cleaned.size());
    for (size_t i = 0; i < cleaned.size(); i++) {
        if (cleaned[i] == '\n') {
            if (i == 0 || cleaned[i - 1] != '\r')
                converted += '\r';
        }
        converted += cleaned[i];
    }
    int len = MultiByteToWideChar(CP_UTF8, 0, converted.c_str(), (int)converted.size(), nullptr, 0);
    if (len > 0) {
        std::vector<wchar_t> wide(len);
        MultiByteToWideChar(CP_UTF8, 0, converted.c_str(), (int)converted.size(), wide.data(), len);
        SetWindowTextW(hEdit, wide.data());
    }
}

// Control IDs
#define IDC_EXECUTOR_COMBO  3001
#define IDC_DISTRO_EDIT     3002
#define IDC_CMD_EDIT        3003
#define IDC_STDIN_EDIT      3004
#define IDC_EXEC_BUTTON     3005
#define IDC_EXIT_CODE_TEXT  3006
#define IDC_STDOUT_EDIT     3007
#define IDC_STDERR_EDIT     3008
#define IDC_UNINSTALL_USBIPD_BUTTON 3009
#define IDC_SHOW_WINDOW_CHECK      3010
#define IDC_UNINSTALL_WSL_BUTTON   3011

// Custom messages for async results
#define WM_USER_EXEC_COMPLETE            (WM_USER + 30)
#define WM_USER_UNINSTALL_USBIPD_RESULT  (WM_USER + 31)
#define WM_USER_UNINSTALL_WSL_RESULT     (WM_USER + 32)

static const wchar_t* DEBUG_WND_CLASS = L"Ext4DebugTestWnd";

struct ExecThreadParam {
    HWND hwnd;
    int executorType;  // 0=PowershellExecutor, 1=WslExecutor, 2=CommandWindow(PS), 3=CommandWindow(WSL)
    std::string command;
    std::string stdinInput;
    std::string distro;
    bool showWindow;
};

struct DebugExecResult {
    DWORD exitCode;
    std::string stdOut;
    std::string stdErr;
};

static DWORD WINAPI ExecThreadProc(LPVOID param) {
    auto* p = (ExecThreadParam*)param;
    HWND hwnd = p->hwnd;

    CommandResult result = { (DWORD)-1, "", "" };

    if (p->executorType == 2 || p->executorType == 3) {
        // ─── 直接测试 CommandWindow ─────────────────────────────────
        bool isPs = (p->executorType == 2);
        std::string shell = isPs
            ? "powershell.exe -NoProfile -NoExit"
            : "wsl.exe --cd ~ -d " + p->distro;
        std::wstring wShell = utils::Utf8ToWString(shell);

        CommandWindow cw;
        if (!cw.Create(wShell, p->showWindow, true)) {
            result.stdErr = "CommandWindow::Create failed";
        } else {
            // WriteLine 发送命令（自动追加 \n，ReadAllLines 转发输出到可见窗口）
            cw.WriteLine(p->command);
            if (!p->stdinInput.empty()) {
                cw.WriteLine(p->stdinInput);
            }

            auto lines = cw.ReadAllLines(30000);
            for (const auto& line : lines) {
                if (!result.stdOut.empty()) result.stdOut += "\n";
                result.stdOut += line;
            }

            // 清理
            cw.WriteLine("exit");
            DWORD ec = 0;
            cw.WaitForExit(5000, &ec);
            result.exitCode = ec;
            cw.Close();
        }
    } else if (p->executorType == 0) {
        PowershellExecutor exec(p->showWindow);
        result = exec.Execute(p->command, p->stdinInput, 30000);
    } else {
        WslExecutor exec(p->distro, p->showWindow);
        result = exec.Execute(p->command, p->stdinInput, 30000);
    }

    auto* res = new DebugExecResult{ result.exitCode, result.stdOut, result.stdErr };
    PostMessageW(hwnd, WM_USER_EXEC_COMPLETE, (WPARAM)res, 0);
    delete p;
    return 0;
}

static LRESULT CALLBACK DebugTestWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        const int M = 10;
        const int LH = 22;
        const int LW = 62;
        const int EW = 200;
        const int CW = 470;

        // Row 1: Executor combo
        CreateWindowExW(0, L"STATIC", L"Executor:",
                        WS_CHILD | WS_VISIBLE, M, M + 3, LW, LH,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"COMBOBOX", L"",
                        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP | CBS_HASSTRINGS,
                        M + LW + 5, M, EW, 200,
                        hwnd, (HMENU)IDC_EXECUTOR_COMBO, nullptr, nullptr);

        // Row 2: Distro edit
        int y2 = M + LH + 8;
        CreateWindowExW(0, L"STATIC", L"Distro:",
                        WS_CHILD | WS_VISIBLE, M, y2 + 3, LW, LH,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"Ubuntu",
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                        M + LW + 5, y2, EW, LH,
                        hwnd, (HMENU)IDC_DISTRO_EDIT, nullptr, nullptr);

        // Row 3: Command edit
        int y3 = y2 + LH + 8;
        CreateWindowExW(0, L"STATIC", L"Command:",
                        WS_CHILD | WS_VISIBLE, M, y3 + 3, LW, LH,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                        M + LW + 5, y3, CW, LH,
                        hwnd, (HMENU)IDC_CMD_EDIT, nullptr, nullptr);

        // Row 4: Stdin edit
        int y4 = y3 + LH + 8;
        CreateWindowExW(0, L"STATIC", L"Stdin:",
                        WS_CHILD | WS_VISIBLE, M, y4 + 3, LW, LH,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | WS_TABSTOP,
                        M + LW + 5, y4, CW, LH,
                        hwnd, (HMENU)IDC_STDIN_EDIT, nullptr, nullptr);

        // Row 5: Execute button + exit code
        int y5 = y4 + LH + 10;
        CreateWindowExW(0, L"BUTTON", L"Execute",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
                        M, y5, 80, 26,
                        hwnd, (HMENU)IDC_EXEC_BUTTON, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"Exit Code:",
                        WS_CHILD | WS_VISIBLE, M + 90, y5 + 3, 70, LH,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"",
                        WS_CHILD | WS_VISIBLE, M + 160, y5 + 3, 100, LH,
                        hwnd, (HMENU)IDC_EXIT_CODE_TEXT, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"显示窗口",
                        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX | WS_TABSTOP,
                        M + 270, y5 + 1, 80, LH,
                        hwnd, (HMENU)IDC_SHOW_WINDOW_CHECK, nullptr, nullptr);

        // Separator
        int ySep = y5 + 32;
        CreateWindowExW(0, L"STATIC", L"",
                        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
                        M, ySep, CW + LW + 5 - M, 2,
                        hwnd, nullptr, nullptr, nullptr);

        // stdout area
        int yOut = ySep + 10;
        CreateWindowExW(0, L"STATIC", L"stdout:",
                        WS_CHILD | WS_VISIBLE, M, yOut + 2, 60, 18,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                        ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL,
                        M, yOut + 22, CW + LW + 5 - M, 110,
                        hwnd, (HMENU)IDC_STDOUT_EDIT, nullptr, nullptr);

        // stderr area
        int yErr = yOut + 22 + 110 + 6;
        CreateWindowExW(0, L"STATIC", L"stderr:",
                        WS_CHILD | WS_VISIBLE, M, yErr + 2, 60, 18,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
                        ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL,
                        M, yErr + 22, CW + LW + 5 - M, 60,
                        hwnd, (HMENU)IDC_STDERR_EDIT, nullptr, nullptr);

        // Presets row
        int yPreset = yErr + 22 + 60 + 8;
        CreateWindowExW(0, L"STATIC", L"Presets:",
                        WS_CHILD | WS_VISIBLE, M, yPreset + 5, 50, 18,
                        hwnd, nullptr, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"卸载 usbipd",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        M + 50, yPreset, 100, 24,
                        hwnd, (HMENU)IDC_UNINSTALL_USBIPD_BUTTON, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"卸载 WSL",
                        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                        M + 155, yPreset, 100, 24,
                        hwnd, (HMENU)IDC_UNINSTALL_WSL_BUTTON, nullptr, nullptr);

        // Populate executor combo
        HWND hCombo = GetDlgItem(hwnd, IDC_EXECUTOR_COMBO);
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"PowershellExecutor");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"WslExecutor");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"CommandWindow(PS)");
        SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)L"CommandWindow(WSL)");
        SendMessageW(hCombo, CB_SETCURSEL, 0, 0);

        // Disable distro edit initially
        EnableWindow(GetDlgItem(hwnd, IDC_DISTRO_EDIT), FALSE);
        EnableWindow(GetDlgItem(hwnd, IDC_SHOW_WINDOW_CHECK), TRUE);

        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_EXECUTOR_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                int sel = (int)SendMessageW(GetDlgItem(hwnd, IDC_EXECUTOR_COMBO),
                                            CB_GETCURSEL, 0, 0);
                // 0=PSExec, 1=WslExec, 2=CmdWin(PS), 3=CmdWin(WSL)
                bool needsDistro = (sel == 1 || sel == 3);
                EnableWindow(GetDlgItem(hwnd, IDC_DISTRO_EDIT), needsDistro);
                bool showWindowEnabled = (sel >= 2);
                EnableWindow(GetDlgItem(hwnd, IDC_SHOW_WINDOW_CHECK), showWindowEnabled);
                if (!showWindowEnabled)
                    SendMessageW(GetDlgItem(hwnd, IDC_SHOW_WINDOW_CHECK), BM_SETCHECK, 0, 0);
            }
            return 0;

        case IDC_UNINSTALL_USBIPD_BUTTON: {
            int ret = MessageBoxW(hwnd,
                L"确认卸载 usbipd？",
                L"卸载 usbipd",
                MB_YESNO | MB_DEFBUTTON2);
            if (ret != IDYES) return 0;

            EnableWindow(GetDlgItem(hwnd, IDC_UNINSTALL_USBIPD_BUTTON), FALSE);
            SetDlgItemTextW(hwnd, IDC_STDOUT_EDIT, L"正在卸载 usbipd (约 30 秒)...");
            SetDlgItemTextW(hwnd, IDC_STDERR_EDIT, L"");

            HANDLE hThread = CreateThread(nullptr, 0,
                [](LPVOID p) -> DWORD {
                    auto* ctx = (std::pair<HWND, std::string>*)p;
                    // 使用完整包 ID 避免 winget 搜索源（搜索会导致下载进度条乱码）
                    auto result = utils::RunProcess(L"winget.exe",
                        L"uninstall \"dorssel.usbipd-win\" --accept-source-agreements",
                        "y\n", 60000);
                    auto* res = new DebugExecResult{ result.exitCode, result.stdOut, result.stdErr };
                    PostMessageW(ctx->first, WM_USER_UNINSTALL_USBIPD_RESULT, (WPARAM)res, 0);
                    delete ctx;
                    return 0;
                }, new std::pair<HWND, std::string>(hwnd, ""), 0, nullptr);
            if (!hThread) {
                EnableWindow(GetDlgItem(hwnd, IDC_UNINSTALL_USBIPD_BUTTON), TRUE);
                SetDlgItemTextW(hwnd, IDC_STDOUT_EDIT, L"内部错误: 线程创建失败");
            } else {
                CloseHandle(hThread);
            }
            return 0;
        }

        case IDC_UNINSTALL_WSL_BUTTON: {
            int ret = MessageBoxW(hwnd,
                L"确认卸载 WSL（取消注册 Ubuntu 发行版）？\n"
                L"WSL 将停止运行，数据不会丢失，可重新安装。",
                L"卸载 WSL",
                MB_YESNO | MB_DEFBUTTON2);
            if (ret != IDYES) return 0;

            EnableWindow(GetDlgItem(hwnd, IDC_UNINSTALL_WSL_BUTTON), FALSE);
            SetDlgItemTextW(hwnd, IDC_STDOUT_EDIT, L"正在卸载 WSL...(约 30 秒)");
            SetDlgItemTextW(hwnd, IDC_STDERR_EDIT, L"");

            HANDLE hThread = CreateThread(nullptr, 0,
                [](LPVOID p) -> DWORD {
                    auto* ctx = (std::pair<HWND, std::string>*)p;
                    HWND hStdout = GetDlgItem(ctx->first, IDC_STDOUT_EDIT);

                    SendMessageW(hStdout, WM_SETTEXT, 0,
                        (LPARAM)L"> Step 1/2: 正在停止 WSL...\r\n");
                    utils::RunProcess(L"wsl.exe", L"--shutdown", "", 15000);

                    SendMessageW(hStdout, WM_SETTEXT, 0,
                        (LPARAM)L"> Step 2/2: 正在卸载 Ubuntu...\r\n");
                    auto result = utils::RunProcess(L"wsl.exe",
                        L"--unregister Ubuntu", "", 30000);
                    auto* res = new DebugExecResult{ result.exitCode, result.stdOut, result.stdErr };
                    PostMessageW(ctx->first, WM_USER_UNINSTALL_WSL_RESULT, (WPARAM)res, 0);
                    delete ctx;
                    return 0;
                }, new std::pair<HWND, std::string>(hwnd, ""), 0, nullptr);
            if (!hThread) {
                EnableWindow(GetDlgItem(hwnd, IDC_UNINSTALL_WSL_BUTTON), TRUE);
                SetDlgItemTextW(hwnd, IDC_STDOUT_EDIT, L"内部错误: 线程创建失败");
            } else {
                CloseHandle(hThread);
            }
            return 0;
        }

        case IDC_EXEC_BUTTON: {
            // Get executor type
            int execType = (int)SendMessageW(GetDlgItem(hwnd, IDC_EXECUTOR_COMBO),
                                              CB_GETCURSEL, 0, 0);

            // Read distro, command, stdin
            wchar_t distroBuf[128] = {};
            wchar_t cmdBuf[2048] = {};
            wchar_t stdinBuf[2048] = {};
            GetDlgItemTextW(hwnd, IDC_DISTRO_EDIT, distroBuf, 128);
            GetDlgItemTextW(hwnd, IDC_CMD_EDIT, cmdBuf, 2048);
            GetDlgItemTextW(hwnd, IDC_STDIN_EDIT, stdinBuf, 2048);

            // Clear previous output
            SetDlgItemTextW(hwnd, IDC_STDOUT_EDIT, L"");
            SetDlgItemTextW(hwnd, IDC_STDERR_EDIT, L"");
            SetDlgItemTextW(hwnd, IDC_EXIT_CODE_TEXT, L"...");

            // Convert to UTF-8
            auto ToUtf8 = [](const wchar_t* src, int srcLen) -> std::string {
                // srcLen == 0: empty string; srcLen == -1: null-terminated (handled by WideCharToMultiByte)
                if (srcLen == 0) return {};
                int len = WideCharToMultiByte(CP_UTF8, 0, src, srcLen, nullptr, 0, nullptr, nullptr);
                if (len <= 0) return {};
                std::vector<char> buf(len);
                WideCharToMultiByte(CP_UTF8, 0, src, srcLen, buf.data(), len, nullptr, nullptr);
                return std::string(buf.data());
            };

            std::string cmd = ToUtf8(cmdBuf, -1);
            std::string stdinStr = ToUtf8(stdinBuf, -1);
            std::string distro = ToUtf8(distroBuf, -1);

            // Disable button during execution
            EnableWindow(GetDlgItem(hwnd, IDC_EXEC_BUTTON), FALSE);

            // Read showWindow checkbox (only applies to WslExecutor)
            bool showWindow = (SendMessageW(GetDlgItem(hwnd, IDC_SHOW_WINDOW_CHECK),
                                            BM_GETCHECK, 0, 0) == BST_CHECKED);

            // Execute on background thread
            auto* param = new ExecThreadParam{ hwnd, execType, cmd, stdinStr, distro, showWindow };
            HANDLE hThread = CreateThread(nullptr, 0, ExecThreadProc, param, 0, nullptr);
            if (!hThread) {
                delete param;
                EnableWindow(GetDlgItem(hwnd, IDC_EXEC_BUTTON), TRUE);
                SetDlgItemTextW(hwnd, IDC_EXIT_CODE_TEXT, L"FAIL");
            } else {
                CloseHandle(hThread);
            }
            return 0;
        }
        }
        return 0;

    case WM_USER_EXEC_COMPLETE: {
        auto* res = (DebugExecResult*)wParam;
        if (res) {
            wchar_t buf[32];
            swprintf_s(buf, L"%lu", res->exitCode);
            SetDlgItemTextW(hwnd, IDC_EXIT_CODE_TEXT, buf);
            SetOutputText(GetDlgItem(hwnd, IDC_STDOUT_EDIT), res->stdOut);
            SetOutputText(GetDlgItem(hwnd, IDC_STDERR_EDIT), res->stdErr);
            delete res;
        }
        EnableWindow(GetDlgItem(hwnd, IDC_EXEC_BUTTON), TRUE);
        return 0;
    }

    case WM_USER_UNINSTALL_USBIPD_RESULT: {
        auto* res = (DebugExecResult*)wParam;
        if (res) {
            wchar_t buf[32];
            swprintf_s(buf, L"uninstall → %lu", res->exitCode);
            SetDlgItemTextW(hwnd, IDC_EXIT_CODE_TEXT, buf);
            SetOutputText(GetDlgItem(hwnd, IDC_STDOUT_EDIT), res->stdOut);
            SetOutputText(GetDlgItem(hwnd, IDC_STDERR_EDIT), res->stdErr);
            delete res;
        }
        EnableWindow(GetDlgItem(hwnd, IDC_UNINSTALL_USBIPD_BUTTON), TRUE);
        return 0;
    }

    case WM_USER_UNINSTALL_WSL_RESULT: {
        auto* res = (DebugExecResult*)wParam;
        if (res) {
            wchar_t buf[32];
            swprintf_s(buf, L"wsl unregister → %lu", res->exitCode);
            SetDlgItemTextW(hwnd, IDC_EXIT_CODE_TEXT, buf);
            SetOutputText(GetDlgItem(hwnd, IDC_STDOUT_EDIT), res->stdOut);
            SetOutputText(GetDlgItem(hwnd, IDC_STDERR_EDIT), res->stdErr);

            if (res->exitCode != 0) {
                std::string err = res->stdErr.empty() ? res->stdOut : res->stdErr;
                MessageBoxW(hwnd,
                    (L"WSL 卸载失败：\n" + utils::Utf8ToWString(err)).c_str(),
                    L"卸载 WSL", MB_OK | MB_ICONERROR);
            }

            delete res;
        }
        EnableWindow(GetDlgItem(hwnd, IDC_UNINSTALL_WSL_BUTTON), TRUE);
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

void ShowDebugTestWindow(HWND parent) {
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = DebugTestWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = DEBUG_WND_CLASS;
        RegisterClassExW(&wc);
        registered = true;
    }

    CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOOLWINDOW,
                    DEBUG_WND_CLASS, L"Debug: Command Layer Test",
                    WS_CAPTION | WS_SYSMENU | WS_VISIBLE | WS_POPUP,
                    CW_USEDEFAULT, CW_USEDEFAULT, 580, 470,
                    parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}

#endif // _DEBUG
