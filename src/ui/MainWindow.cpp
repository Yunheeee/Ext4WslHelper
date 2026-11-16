#include "MainWindow.h"
#ifdef _DEBUG
#include "DebugTestWindow.h"
#endif
#include "../common/Logger.h"
#include "../common/Utils.h"
#include "../common/WslSetup.h"
#include "Resource.h"
#define _WIN32_WINNT 0x0601
#include <commctrl.h>
#include <dbt.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>
#include <ctime>
#include <iomanip>
#include <array>
#include <cctype>

#pragma comment(lib, "comctl32.lib")

#ifdef _DEBUG
#define IDC_DEBUG_TEST_BUTTON 1011
#endif

static const wchar_t* CLASS_NAME = L"Ext4WslHelperMainWnd";

// ─── Constructor / Destructor ─────────────────────────────────────────────

MainWindow::MainWindow(HINSTANCE hInstance)
    : hInstance_(hInstance), hwnd_(nullptr)
    , psExec_(false)
    , usbipdMgr_(&psExec_), wslMgr_(&psExec_), devMgr_(&psExec_)
    , mountMgr_(&usbipdMgr_, &wslMgr_)
    , envService_(&psExec_)
    , deviceCombo_(nullptr), detectButton_(nullptr)
    , logEdit_(nullptr), toggleLogButton_(nullptr), exitButton_(nullptr)
    , envStatus_{}
    , envInitButton_(nullptr)
    , windowsAccessEdit_(nullptr), mountStatusText_(nullptr)
    , mountButton_(nullptr), unmountButton_(nullptr)
    , hFont_(nullptr), hMonoFont_(nullptr)
    , hBrushGreen_(CreateSolidBrush(RGB(0, 170, 0)))
    , hBrushYellow_(CreateSolidBrush(RGB(210, 180, 0)))
    , hBrushRed_(CreateSolidBrush(RGB(200, 40, 40)))
    , logExpanded_(true), isWorking_(false), isDetecting_(false) {
}

MainWindow::~MainWindow() {
    if (hFont_) DeleteObject(hFont_);
    if (hMonoFont_) DeleteObject(hMonoFont_);
    if (hBrushGreen_) DeleteObject(hBrushGreen_);
    if (hBrushYellow_) DeleteObject(hBrushYellow_);
    if (hBrushRed_) DeleteObject(hBrushRed_);
}

// ─── Window creation ──────────────────────────────────────────────────────

bool MainWindow::Create() {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance_;
    wc.hIcon = LoadIconW(hInstance_, MAKEINTRESOURCEW(IDI_MAIN_ICON));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = CLASS_NAME;

    RegisterClassExW(&wc);

    hwnd_ = CreateWindowExW(
        0, CLASS_NAME, L"Ext4WslHelper — EXT4 SD 卡访问工具",
        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX & ~WS_THICKFRAME,
        CW_USEDEFAULT, CW_USEDEFAULT, 570, 580,
        nullptr, nullptr, hInstance_, this);

    if (!hwnd_) return false;

    // 注册设备通知
    DEV_BROADCAST_DEVICEINTERFACE_W notifyFilter = {};
    notifyFilter.dbcc_size = sizeof(notifyFilter);
    notifyFilter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    notifyFilter.dbcc_classguid =
        { 0x53F5630D, 0xB6BF, 0x11D0, { 0x94, 0xF2, 0x00, 0xA0, 0xC9, 0x1E, 0xFB, 0x8B } };
    RegisterDeviceNotificationW(hwnd_, &notifyFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

    INITCOMMONCONTROLSEX icc = {};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    CreateControls();

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);

    AppendLog(L"> 初始化完成");

    return true;
}

int MainWindow::Run() {
#ifdef _DEBUG
    // executor 输出转发到 debug 窗口
    psExec_.SetOutputSink([](const std::string& line) {
        Logger::Instance().Info(line);
    });
#endif

    // 启动后自动运行环境检测和设备枚举
    RunEnvCheck();
    RefreshDeviceList();
    UpdateButtonState();

    MSG msg = {};
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

// ─── Window procedure ─────────────────────────────────────────────────────

LRESULT CALLBACK MainWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_CREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          (LONG_PTR)((CREATESTRUCT*)lParam)->lpCreateParams);
        return 0;
    }
    MainWindow* self = (MainWindow*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (self && msg == WM_DESTROY) {
        self->OnDestroy();
        return 0;
    }
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lParam;
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 400;
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDC_DETECT_BUTTON:
            OnDetectDevice();
            return 0;
        case IDC_MOUNT_BUTTON:
            if (!isWorking_) OnMount();
            return 0;
        case IDC_UNMOUNT_BUTTON:
            if (!isWorking_) OnUnmount();
            return 0;
        case IDC_EXIT_BUTTON:
            DestroyWindow(hwnd_);
            return 0;
        case IDC_TOGGLE_LOG_BUTTON:
            OnToggleLog();
            return 0;
        case IDC_DEVICE_COMBO:
            if (HIWORD(wParam) == CBN_SELCHANGE) {
                UpdateButtonState();
                UpdateMountInfo();
            }
            return 0;
        case IDC_ENV_INIT_BUTTON:
            if (!isWorking_) OnEnvInit();
            return 0;
#ifdef _DEBUG
        case IDC_DEBUG_TEST_BUTTON:
            ShowDebugTestWindow(hwnd_);
            return 0;
#endif
        }
        return 0;

    case WM_DEVICECHANGE:
        if (!isWorking_) {
            if (wParam == DBT_DEVICEARRIVAL ||
                wParam == DBT_DEVICEREMOVECOMPLETE) {
                OnDeviceChange();
            }
        }
        return TRUE;

    case WM_USER_MOUNT_COMPLETE: {
        MountSession* s = (MountSession*)wParam;
        EnableControls(true);
        isWorking_ = false;
        if (s && s->isMounted) {
            AppendLog(L"✓ 挂载成功");
            SetMountStatus(L"✓ 已挂载");
        } else {
            AppendLog(L"✗ 挂载失败");
            SetMountStatus(L"✗ 挂载失败，请重试", true);
        }
        UpdateButtonState();
        UpdateMountInfo();
        if (s) delete s;
        return 0;
    }

    case WM_USER_UNMOUNT_COMPLETE: {
        bool ok = (wParam != 0);
        EnableControls(true);
        isWorking_ = false;
        AppendLog(ok ? L"✓ 已安全卸载" : L"✗ 卸载失败");
        SetMountStatus(ok ? L"○ 已卸载" : L"✗ 卸载失败", !ok);
        UpdateButtonState();
        UpdateMountInfo();
        return 0;
    }

    case WM_USER_LOG_APPEND: {
        wchar_t* text = (wchar_t*)lParam;
        if (text) {
            int len = GetWindowTextLengthW(logEdit_);
            SendMessageW(logEdit_, EM_SETSEL, len, len);
            SendMessageW(logEdit_, EM_REPLACESEL, FALSE, (LPARAM)text);
            SendMessageW(logEdit_, EM_SCROLLCARET, 0, 0);
            delete[] text;
        }
        return 0;
    }

    case WM_USER_ENV_CHECK_COMPLETE: {
        EnvironmentStatus* status = (EnvironmentStatus*)wParam;
        if (status) {
            OnEnvCheckComplete(status);
            delete status;
        }
        return 0;
    }

    case WM_USER_ENV_INIT_COMPLETE: {
        // 0=失败, 1=成功, 2=功能已启用但需要重启
        bool ok = (wParam == 1);
        bool needsReboot = (wParam == 2);
        EnableControls(true);
        isWorking_ = false;
        if (ok) {
            AppendLog(L"✓ 环境初始化完成");
            SetMountStatus(L"✓ 环境已就绪");
        } else if (needsReboot) {
            AppendLog(L"⚠ WSL 功能已启用，但组件需要重启 Windows 才能生效");
            SetMountStatus(L"⚠ 需要重启 Windows 后重试", true);
            MessageBoxW(hwnd_,
                L"WSL 功能已启用，但相关组件需要重启 Windows 后才能生效。\n\n"
                L"请重启计算机，然后重新运行 Ext4WslHelper 并再次点击「环境初始化」。",
                L"需要重启 Windows",
                MB_OK | MB_ICONWARNING);
        } else {
            AppendLog(L"✗ 环境初始化未完全成功，请检查日志");
            SetMountStatus(L"⚠ 环境初始化未完成", true);
        }
        // 重新检测环境状态
        RunEnvCheck();
        RefreshDeviceList();
        UpdateButtonState();
        return 0;
    }

    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        HWND hwndCtrl = (HWND)lParam;
        for (int i = 0; i < 5; i++) {
            if (hwndCtrl == envStatus_[i].icon) {
                SetBkMode(hdc, TRANSPARENT);
                wchar_t ch[16] = {};
                GetWindowTextW(envStatus_[i].text, ch, 16);
                if (ch[0] == L'✓')
                    return (LRESULT)hBrushGreen_;
                else if (ch[0] == L'✗')
                    return (LRESULT)hBrushRed_;
                else
                    return (LRESULT)hBrushYellow_;
            }
        }
        break;
    }
    }
    return DefWindowProcW(hwnd_, msg, wParam, lParam);
}

void MainWindow::OnDestroy() {
    PostQuitMessage(0);
}

// ─── Controls ─────────────────────────────────────────────────────────────

void MainWindow::CreateControls() {
    const int M = 14;
    const int W = 542;  // 内容区宽度

    NONCLIENTMETRICSW ncm = { sizeof(ncm) };
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    hFont_ = CreateFontIndirectW(&ncm.lfMessageFont);
    int fontHeight = abs(ncm.lfMessageFont.lfHeight);

    const int TH = max(18, fontHeight + 2);
    const int LH = max(28, fontHeight + 10);

    int y = M;

    // ═════════════════════════════════════════════════════════════════════
    // Area 1: 环境检测
    // ═════════════════════════════════════════════════════════════════════

    CreateWindowExW(0, L"STATIC", L"▎环境检测",
        WS_CHILD | WS_VISIBLE, M, y, 120, TH,
        hwnd_, nullptr, hInstance_, nullptr);
    y += TH + 6;

    // 五项状态：虚拟化 / WSL 功能 / WSL 内核 / Alpine / usbipd
    const wchar_t* envDefaultLabels[] = {
        L"虚拟化开启", L"WSL开启", L"WSL2内核", L"Alpine安装", L"usbipd安装"
    };

    for (int i = 0; i < 5; i++) {
        // 指示灯 — SS_SUNKEN 彩色方块
        envStatus_[i].icon = CreateWindowExW(0, L"STATIC", L"",
            WS_CHILD | WS_VISIBLE | SS_SUNKEN,
            M, y + 2, 14, TH - 2,
            hwnd_, nullptr, hInstance_, nullptr);
        envStatus_[i].text = CreateWindowExW(0, L"STATIC",
            envDefaultLabels[i],
            WS_CHILD | WS_VISIBLE, M + 20, y, 380, TH,
            hwnd_, nullptr, hInstance_, nullptr);
        y += TH + 2;
    }
    y += 4;

    // 环境操作按钮
    envInitButton_ = CreateWindowExW(0, L"BUTTON", L"⚙ 环境初始化",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        M, y, 140, LH,
        hwnd_, (HMENU)IDC_ENV_INIT_BUTTON, hInstance_, nullptr);
    y += LH + 8;

    // ── 分隔线 ────────────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        M, y, W, 2,
        hwnd_, nullptr, hInstance_, nullptr);
    y += 10;

    // ═════════════════════════════════════════════════════════════════════
    // Area 2: 设备选择
    // ═════════════════════════════════════════════════════════════════════

    CreateWindowExW(0, L"STATIC", L"▎设备选择",
        WS_CHILD | WS_VISIBLE, M, y, 120, TH,
        hwnd_, nullptr, hInstance_, nullptr);
    y += TH + 6;

    CreateWindowExW(0, L"STATIC", L"USB 设备:",
        WS_CHILD | WS_VISIBLE, M, y + 4, 70, TH,
        hwnd_, nullptr, hInstance_, nullptr);
    deviceCombo_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST |
        WS_TABSTOP | CBS_HASSTRINGS,
        M + 72, y, 386, 200,
        hwnd_, (HMENU)IDC_DEVICE_COMBO, hInstance_, nullptr);
    detectButton_ = CreateWindowExW(0, L"BUTTON", L"🔍 检测",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        M + 72 + 386 + 4, y, 66, LH,
        hwnd_, (HMENU)IDC_DETECT_BUTTON, hInstance_, nullptr);
    y += LH + 10;

    // ── 分隔线 ────────────────────────────────────────────────────────────
    CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_ETCHEDHORZ,
        M, y, W, 2,
        hwnd_, nullptr, hInstance_, nullptr);
    y += 10;

    // ═════════════════════════════════════════════════════════════════════
    // Area 3: 挂载操作
    // ═════════════════════════════════════════════════════════════════════

    CreateWindowExW(0, L"STATIC", L"▎挂载操作",
        WS_CHILD | WS_VISIBLE, M, y, 120, TH,
        hwnd_, nullptr, hInstance_, nullptr);
    y += TH + 6;

    // Windows 访问路径（只读）
    CreateWindowExW(0, L"STATIC", L"Windows 访问路径:",
        WS_CHILD | WS_VISIBLE, M, y + 4, 130, TH,
        hwnd_, nullptr, hInstance_, nullptr);
    windowsAccessEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_READONLY | ES_AUTOHSCROLL,
        M + 132, y, W - 132, LH,
        hwnd_, (HMENU)IDC_WINDOWS_ACCESS_EDIT, hInstance_, nullptr);
    y += LH + 6;

    // 挂载状态
    mountStatusText_ = CreateWindowExW(0, L"STATIC", L"未挂载",
        WS_CHILD | WS_VISIBLE, M, y, W, TH,
        hwnd_, (HMENU)IDC_MOUNT_STATUS_TEXT, hInstance_, nullptr);
    y += TH + 8;

    // 挂载 / 卸载按钮
    mountButton_ = CreateWindowExW(0, L"BUTTON", L"🔗 挂载",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        M, y, 110, LH,
        hwnd_, (HMENU)IDC_MOUNT_BUTTON, hInstance_, nullptr);
    unmountButton_ = CreateWindowExW(0, L"BUTTON", L"🔓 卸载",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
        M + 118, y, 110, LH,
        hwnd_, (HMENU)IDC_UNMOUNT_BUTTON, hInstance_, nullptr);
    y += LH + 10;

    // ═════════════════════════════════════════════════════════════════════
    // 日志区域
    // ═════════════════════════════════════════════════════════════════════

    int yLog = y;
    logEdit_ = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_MULTILINE | ES_READONLY |
        ES_AUTOHSCROLL | ES_AUTOVSCROLL | WS_VSCROLL,
        M, yLog, W, 130,
        hwnd_, (HMENU)IDC_LOG_EDIT, hInstance_, nullptr);
    y = yLog + 130 + 6;

    // ═════════════════════════════════════════════════════════════════════
    // 底部按钮
    // ═════════════════════════════════════════════════════════════════════

    toggleLogButton_ = CreateWindowExW(0, L"BUTTON", L"▼ 隐藏详情",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        M, y, 110, 28,
        hwnd_, (HMENU)IDC_TOGGLE_LOG_BUTTON, hInstance_, nullptr);
#ifdef _DEBUG
    CreateWindowExW(0, L"BUTTON", L"测试",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        M + 115, y, 50, 28,
        hwnd_, (HMENU)IDC_DEBUG_TEST_BUTTON, hInstance_, nullptr);
#endif
    exitButton_ = CreateWindowExW(0, L"BUTTON", L"退出",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        490, y, 70, 28,
        hwnd_, (HMENU)IDC_EXIT_BUTTON, hInstance_, nullptr);

    // ── 字体 ────────────────────────────────────────────────────────────
    EnumChildWindows(hwnd_, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)hFont_);

    hMonoFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(logEdit_, WM_SETFONT, (WPARAM)hMonoFont_, TRUE);
    SendMessageW(logEdit_, EM_SETLIMITTEXT, 0x7FFFFFFF, 0);

    AppendLog(L"> Ext4WslHelper 启动完成");
    AppendLog(L"> 环境检测中...");
}

// ─── Refresh / Update ─────────────────────────────────────────────────────

void MainWindow::RefreshDeviceList() {
    std::string prevBusId;
    int prevSel = (int)SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0);
    if (prevSel >= 0 && prevSel < (int)currentDevices_.size()) {
        prevBusId = currentDevices_[prevSel].busId;
    }

    currentDevices_ = devMgr_.EnumerateDevices();
    SendMessageW(deviceCombo_, CB_RESETCONTENT, 0, 0);
    for (const auto& d : currentDevices_) {
        std::wstring txt = utils::Utf8ToWString(d.description);
        if (!d.busId.empty())
            txt += L" (" + utils::Utf8ToWString(d.busId) + L")";
        if (d.IsAttached())
            txt += L" [已附加]";
        SendMessageW(deviceCombo_, CB_ADDSTRING, 0, (LPARAM)txt.c_str());
    }
    if (!currentDevices_.empty()) {
        int selectIdx = 0;
        if (!prevBusId.empty()) {
            for (size_t i = 0; i < currentDevices_.size(); i++) {
                if (currentDevices_[i].busId == prevBusId) {
                    selectIdx = (int)i;
                    break;
                }
            }
        } else {
            for (size_t i = 0; i < currentDevices_.size(); i++) {
                if (currentDevices_[i].description.find("大容量存储") != std::string::npos ||
                    currentDevices_[i].description.find("Mass Storage") != std::string::npos) {
                    selectIdx = (int)i;
                    break;
                }
            }
        }
        SendMessageW(deviceCombo_, CB_SETCURSEL, selectIdx, 0);
    } else {
        AppendLog(L"⚠ 未检测到 USB 设备");
    }
    UpdateButtonState();
    UpdateMountInfo();
}

void MainWindow::UpdateButtonState() {
    int di = (int)SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0);
    bool hasDevice = (di >= 0 && di < (int)currentDevices_.size());

    // 非大容量存储设备禁用挂载按钮
    bool isMassStorage = false;
    if (hasDevice) {
        const auto& desc = currentDevices_[di].description;
        isMassStorage = (desc.find("大容量存储") != std::string::npos ||
                         desc.find("Mass Storage") != std::string::npos);
    }

    EnableWindow(mountButton_, hasDevice && isMassStorage);
    EnableWindow(unmountButton_, hasDevice && isMassStorage);
    if (hasDevice && !isMassStorage) {
        SetWindowTextW(mountButton_, L"🔗 挂载");
        SetWindowTextW(unmountButton_, L"🔓 卸载");
    } else if (hasDevice) {
        auto s = mountMgr_.GetSession(currentDevices_[di].busId);
        SetWindowTextW(mountButton_, s.isMounted ? L"🔗 重新挂载" : L"🔗 挂载");
        SetWindowTextW(unmountButton_, s.isMounted ? L"🔓 卸载" : L"🔓 卸载");
    }
}

void MainWindow::UpdateMountInfo() {
    int di = (int)SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0);
    if (di >= 0 && di < (int)currentDevices_.size()) {
        auto s = mountMgr_.GetSession(currentDevices_[di].busId);
        if (s.isMounted && !s.windowsPath.empty()) {
            SetWindowTextW(windowsAccessEdit_,
                utils::Utf8ToWString(s.windowsPath).c_str());
            SetMountStatus(L"✓ 已挂载");
        } else {
            SetWindowTextW(windowsAccessEdit_, L"");
            SetMountStatus(L"○ 未挂载");
        }
    } else {
        SetWindowTextW(windowsAccessEdit_, L"");
        SetMountStatus(L"○ 未挂载");
    }
}

// ─── Log / Status ─────────────────────────────────────────────────────────

void MainWindow::AppendLog(const wchar_t* text) {
    if (!logEdit_ || !text) return;
    auto t = std::time(nullptr); std::tm tm; localtime_s(&tm, &t);
    wchar_t pre[32]; wcsftime(pre, 32, L"[%H:%M:%S] ", &tm);
    std::wstring line = pre + std::wstring(text) + L"\r\n";
    wchar_t* copy = new wchar_t[line.size() + 1];
    wcscpy_s(copy, line.size() + 1, line.c_str());
    PostMessageW(hwnd_, WM_USER_LOG_APPEND, 0, (LPARAM)copy);
}

void MainWindow::SetMountStatus(const wchar_t* text, bool /*isError*/) {
    if (mountStatusText_) {
        SetWindowTextW(mountStatusText_, text);
    }
}

void MainWindow::EnableControls(bool enable) {
    EnableWindow(deviceCombo_, enable);
    EnableWindow(detectButton_, enable);
    EnableWindow(mountButton_, enable);
    EnableWindow(unmountButton_, enable);
    EnableWindow(exitButton_, enable);
    EnableWindow(envInitButton_, enable);
}

// ─── Mount / Unmount ──────────────────────────────────────────────────────

void MainWindow::OnMount() {
    int di = (int)SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0);
    if (di < 0 || di >= (int)currentDevices_.size()) return;
    const auto& dev = currentDevices_[di];

    // 仅支持大容量存储设备
    if (dev.description.find("大容量存储") == std::string::npos &&
        dev.description.find("Mass Storage") == std::string::npos) {
        AppendLog(L"✗ 仅支持 USB 大容量存储设备（SD 卡/读卡器）");
        SetMountStatus(L"✗ 不支持的设备类型", true);
        return;
    }

    // 检查是否已挂载
    MountSession existing = mountMgr_.GetSession(dev.busId);
    if (existing.isMounted) {
        AppendLog(L"> 设备已挂载，请先卸载");
        return;
    }

    isWorking_ = true;
    EnableControls(false);
    AppendLog((L"> 挂载: " + utils::Utf8ToWString(dev.busId) + L" → Alpine").c_str());
    SetMountStatus(L"❄ 正在挂载...");

    // Alpine 默认 root 用户，无需 sudo 密码
    MountThreadParam* p = new MountThreadParam{this, dev.busId, ""};
    HANDLE h = CreateThread(nullptr, 0, MountThreadProc, p, 0, nullptr);
    if (!h) { delete p; isWorking_ = false; EnableControls(true); AppendLog(L"✗ 内部错误"); }
    else CloseHandle(h);
}

void MainWindow::OnUnmount() {
    int di = (int)SendMessageW(deviceCombo_, CB_GETCURSEL, 0, 0);
    if (di < 0 || di >= (int)currentDevices_.size()) return;

    isWorking_ = true;
    EnableControls(false);
    AppendLog((L"> 卸载: " + utils::Utf8ToWString(currentDevices_[di].busId)).c_str());
    SetMountStatus(L"❄ 正在卸载...");

    MountThreadParam* p = new MountThreadParam{this, currentDevices_[di].busId, ""};
    HANDLE h = CreateThread(nullptr, 0, UnmountThreadProc, p, 0, nullptr);
    if (!h) { delete p; isWorking_ = false; EnableControls(true); AppendLog(L"✗ 内部错误"); }
    else CloseHandle(h);
}

// ─── Background threads ───────────────────────────────────────────────────

DWORD WINAPI MainWindow::MountThreadProc(LPVOID param) {
    MountThreadParam* p = (MountThreadParam*)param;
    MountSession result;
    result.status = MountSession::Status::Error;

    try {
        if (p->wnd) {
            result = p->wnd->mountMgr_.AttachAndMount(p->busId, "Alpine", p->password);
        }
    } catch (...) {
        result.isMounted = false;
        result.status = MountSession::Status::Error;
        Logger::Instance().Error("[MountThread] Exception in AttachAndMount");
    }

    if (p->wnd && IsWindow(p->wnd->hwnd_)) {
        PostMessageW(p->wnd->hwnd_, WM_USER_MOUNT_COMPLETE,
                     (WPARAM)new MountSession(result), 0);
    }
    delete p;
    return 0;
}

DWORD WINAPI MainWindow::UnmountThreadProc(LPVOID param) {
    MountThreadParam* p = (MountThreadParam*)param;
    bool ok = false;

    try {
        if (p->wnd) {
            ok = p->wnd->mountMgr_.UnmountAndDetach(p->busId, p->password);
        }
    } catch (...) {
        Logger::Instance().Error("[UnmountThread] Exception in UnmountAndDetach");
    }

    if (p->wnd && IsWindow(p->wnd->hwnd_)) {
        PostMessageW(p->wnd->hwnd_, WM_USER_UNMOUNT_COMPLETE, ok ? 1 : 0, 0);
    }
    delete p;
    return 0;
}

// ─── Toggle Log ───────────────────────────────────────────────────────────

void MainWindow::OnToggleLog() {
    logExpanded_ = !logExpanded_;

    RECT rc;
    GetWindowRect(logEdit_, &rc);
    MapWindowPoints(HWND_DESKTOP, hwnd_, (LPPOINT)&rc, 2);
    int yLog = rc.top;
    int logH = logExpanded_ ? 130 : 0;
    int btnY = yLog + logH + 6;

    SetWindowPos(logEdit_, nullptr, rc.left, yLog, 542, logH, SWP_NOZORDER);
    ShowWindow(logEdit_, logExpanded_ ? SW_SHOW : SW_HIDE);
    SetWindowPos(toggleLogButton_, nullptr, rc.left, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
#ifdef _DEBUG
    SetWindowPos(GetDlgItem(hwnd_, IDC_DEBUG_TEST_BUTTON), nullptr, rc.left + 115, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
#endif
    SetWindowPos(exitButton_, nullptr, 490, btnY, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

    SetWindowTextW(toggleLogButton_,
                   logExpanded_ ? L"▼ 隐藏详情" : L"▶ 显示详情");
}

// ─── Device Change ────────────────────────────────────────────────────────

void MainWindow::OnDeviceChange() {
    if (isWorking_) return;
    static DWORD s_lastRefresh = 0;
    DWORD now = GetTickCount();
    if (now - s_lastRefresh < 500) return;
    s_lastRefresh = now;

    RefreshDeviceList();
    UpdateMountInfo();

    // 检测模式：自动选中新设备
    if (isDetecting_) {
        for (const auto& dev : currentDevices_) {
            bool wasPresent = false;
            for (const auto& existing : detectInitialBusIds_) {
                if (existing == dev.busId) { wasPresent = true; break; }
            }
            if (wasPresent) continue;
            if (dev.description.find("大容量存储") != std::string::npos ||
                dev.description.find("Mass Storage") != std::string::npos) {
                for (size_t i = 0; i < currentDevices_.size(); i++) {
                    if (currentDevices_[i].busId == dev.busId) {
                        SendMessageW(deviceCombo_, CB_SETCURSEL, i, 0);
                        UpdateButtonState();
                        UpdateMountInfo();
                        break;
                    }
                }
                AppendLog(L"✓ 检测到新设备");
                SetMountStatus(L"✓ 已就绪");
                SetWindowTextW(detectButton_, L"🔍 检测");
                detectInitialBusIds_.clear();
                isDetecting_ = false;
                break;
            }
        }
    }
}

// ─── Device Detection ─────────────────────────────────────────────────────

void MainWindow::OnDetectDevice() {
    // Toggle detection mode
    if (isDetecting_) {
        CancelDetect();
        return;
    }

    if (isWorking_) return;
    isDetecting_ = true;

    detectInitialBusIds_.clear();
    for (const auto& dev : currentDevices_) {
        detectInitialBusIds_.push_back(dev.busId);
    }

    SetWindowTextW(detectButton_, L"取消");
    AppendLog(L"> 等待 USB 设备插入...");
    SetMountStatus(L"❄ 等待设备...");
}

void MainWindow::CancelDetect() {
    if (!isDetecting_) return;
    SetWindowTextW(detectButton_, L"🔍 检测");
    detectInitialBusIds_.clear();
    isDetecting_ = false;
    AppendLog(L"> 已取消");
    SetMountStatus(L"○ 未挂载");
}

// ─── Environment Check ────────────────────────────────────────────────────

void MainWindow::RunEnvCheck() {
    if (isWorking_) return;
    AppendLog(L"> 环境检测...");

    // 重置指示灯为黄色（检测中）
    for (int i = 0; i < 5; i++) {
        SetWindowTextW(envStatus_[i].text, L"检测中...");
        InvalidateRect(envStatus_[i].icon, nullptr, TRUE);
    }

    EnvironmentStatus* status = new EnvironmentStatus();
    IEnvironmentService* svc = &envService_;
    HWND hwnd = hwnd_;

    struct EnvCheckThreadParam {
        IEnvironmentService* svc;
        EnvironmentStatus* status;
        HWND hwnd;
    };
    EnvCheckThreadParam* p = new EnvCheckThreadParam{ svc, status, hwnd };

    HANDLE h = CreateThread(nullptr, 0, [](LPVOID param) -> DWORD {
        auto* p = (EnvCheckThreadParam*)param;
        *p->status = p->svc->CheckAll();
        PostMessageW(p->hwnd, WM_USER_ENV_CHECK_COMPLETE, (WPARAM)p->status, 0);
        delete p;
        return 0;
    }, p, 0, nullptr);

    if (!h) {
        delete p; delete status;
        AppendLog(L"✗ 环境检测启动失败");
    } else {
        CloseHandle(h);
    }
}

void MainWindow::OnEnvCheckComplete(EnvironmentStatus* status) {
    if (!status) return;

    // 状态标签名（与 CreateControls 中一致）
    static const wchar_t* s_labels[] = {
        L"虚拟化开启", L"WSL开启", L"WSL2内核", L"Alpine安装", L"usbipd安装"
    };

    // 更新 5 项状态指示灯
    // 仅有 ✓/✗ 前缀识别状态，红绿灯颜色通过 WM_CTLCOLORSTATIC 从首字符判断
    bool anyNeedsReboot = false;
    for (int i = 0; i < 5; i++) {
        bool ready = false;
        bool needsReboot = false;
        const std::string* detail = nullptr;
        switch (i) {
        case 0:
            ready = status->virt.isReady();
            needsReboot = status->virt.needsReboot;
            detail = &status->virt.detail;
            break;
        case 1:
            ready = status->wslFeature.isReady();
            needsReboot = status->wslFeature.needsReboot;
            detail = &status->wslFeature.detail;
            break;
        case 2:
            ready = status->wsl.isReady();
            detail = &status->wsl.detail;
            break;
        case 3:
            ready = status->alpine.isReady();
            detail = &status->alpine.detail;
            break;
        case 4:
            ready = status->usbipd.isReady();
            detail = &status->usbipd.detail;
            break;
        }
        anyNeedsReboot = anyNeedsReboot || needsReboot;

        std::wstring label;
        if (ready) label = L"✓ ";
        else if (needsReboot) label = L"⟳ ";
        else label = L"✗ ";
        label += s_labels[i];
        if (needsReboot) label += L"（需重启）";

        SetWindowTextW(envStatus_[i].text, label.c_str());
        InvalidateRect(envStatus_[i].icon, nullptr, TRUE);

        if (!ready && detail && !detail->empty()) {
            AppendLog((label + L": " + utils::Utf8ToWString(*detail)).c_str());
        }
    }

    bool allReady = status->allReady();
    if (anyNeedsReboot) {
        SetMountStatus(L"⚠ 需要重启 Windows 后重试");
        AppendLog(L"⚠ WSL 功能已启用，但组件需要重启 Windows 后才能生效，请重启后重新检测");
    } else {
        SetMountStatus(allReady ? L"✓ 环境已就绪" : L"⚠ 部分环境未就绪");
    }

    if (allReady) {
        AppendLog(L"✓ 环境检测通过");
    } else {
        std::wstring missing;
        if (!status->virt.isReady()) missing += L"虚拟化 ";
        if (!status->wslFeature.isReady()) missing += L"WSL ";
        if (!status->wsl.isReady()) missing += L"WSL2内核 ";
        if (!status->alpine.isReady()) missing += L"Alpine ";
        if (!status->usbipd.isReady()) missing += L"usbipd ";
        AppendLog((L"⚠ 未就绪: " + missing + L"— 请点击「环境初始化」").c_str());
    }
}

// ─── Environment Init ─────────────────────────────────────────────────────

void MainWindow::OnEnvInit() {
    if (isWorking_) return;

    // 先在 UI 线程快速检查离线包（仅文件系统操作，无外部进程）
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring exeDir = exePath;
    size_t pos = exeDir.rfind(L'\\');
    if (pos != std::wstring::npos) exeDir = exeDir.substr(0, pos);

    // 默认搜索路径改为 exe 同目录下的 packages\ 子目录
    std::wstring pkgDir = exeDir + L"\\packages";

    // 查找各离线包（新版 WSL 包名为 wsl.<version>.x64.msi；
    // 旧版 wsl_update_x64.msi 内核 5.10.16，不满足 usbipd 要求，必须排除）
    std::wstring wslMsi = wslsetup::FindWslPackage(pkgDir);
    if (wslMsi.empty())
        wslMsi = wslsetup::FindWslPackage(exeDir);

    std::wstring alpinePkg = wslsetup::FindPackageFile(pkgDir, L"Alpine.zip");
    if (alpinePkg.empty())
        alpinePkg = wslsetup::FindPackageFile(pkgDir, L"alpine*.zip");
    if (alpinePkg.empty())
        alpinePkg = wslsetup::FindPackageFile(pkgDir, L"alpine*.tar*");
    if (alpinePkg.empty())
        alpinePkg = wslsetup::FindPackageFile(pkgDir, L"Alpine*.tar*");

    std::wstring usbipdMsi = wslsetup::FindPackageFile(pkgDir, L"usbipd*.msi");
    if (usbipdMsi.empty())
        usbipdMsi = wslsetup::FindPackageFile(pkgDir, L"*usbipd*.msi");

    // 提示缺失的离线包
    if (wslMsi.empty() || alpinePkg.empty() || usbipdMsi.empty()) {
        std::wstring msg = L"未找到全部离线安装包，请把安装包放到程序目录的 packages 文件夹。\n\n";
        if (wslMsi.empty()) msg += L"  • 新版 WSL 安装包 (wsl.<版本>.x64.msi，需 WSL ≥2.x / 内核 ≥5.10.60.1)\n"
                                   L"    （旧版 wsl_update_x64.msi 内核 5.10.16 过旧，已不再支持）\n";
        if (alpinePkg.empty()) msg += L"  • Alpine 发行包 (Alpine.zip / alpine*.tar*)\n";
        if (usbipdMsi.empty()) msg += L"  • usbipd-win MSI (usbipd*.msi)\n";
        msg += L"\n已自动查找目录: ";
        msg += pkgDir;
        MessageBoxW(hwnd_, msg.c_str(), L"缺少离线安装包", MB_OK | MB_ICONWARNING);
        return;
    }

    isWorking_ = true;
    EnableControls(false);
    AppendLog(L"> 开始环境初始化...");
    AppendLog(L"> 检测环境状态并安装缺失组件");

    // 后台线程执行 CheckAll + 安装
    EnvInitThreadParam* p = new EnvInitThreadParam{
        this, wslMsi, alpinePkg, usbipdMsi
    };

    HANDLE h = CreateThread(nullptr, 0, EnvInitThreadProc, p, 0, nullptr);
    if (!h) {
        delete p;
        isWorking_ = false;
        EnableControls(true);
        AppendLog(L"✗ 内部错误");
    } else {
        CloseHandle(h);
    }
}

DWORD WINAPI MainWindow::EnvInitThreadProc(LPVOID param) {
    EnvInitThreadParam* p = (EnvInitThreadParam*)param;
    HWND hwnd = p->wnd->hwnd_;

    auto postLog = [hwnd](const wchar_t* msg) {
        wchar_t* copy = new wchar_t[wcslen(msg) + 1];
        wcscpy_s(copy, wcslen(msg) + 1, msg);
        PostMessageW(hwnd, WM_USER_LOG_APPEND, 0, (LPARAM)copy);
    };

    postLog(L"> 检测当前环境状态...");
    EnvironmentStatus status = p->wnd->envService_.CheckAll();

    bool allOk = true;
    bool rebootRequired = false;

    // 启用功能后再次查询 dism，识别 "Enable Pending / 启用挂起"。
    // 这是“已写入标志位但组件未生效”的最直接证据。
    auto featurePendingReboot = [](const wchar_t* featureName) -> bool {
        std::wstring args = L"/online /get-featureinfo /featurename:";
        args += featureName;
        auto info = utils::RunProcess(L"dism.exe", args, "", 30000);
        return EnvironmentService::ParseDismFeatureState(info.stdOut) ==
               DismFeatureState::EnablePending;
    };

    // 功能标志位已写入但组件未落地：继续安装/导入只会得到 1603 或挂载失败，
    // 直接引导用户重启。
    if (status.wslFeature.needsReboot || status.virt.needsReboot) {
        postLog((L"⚠ " + utils::Utf8ToWString(
            status.wslFeature.needsReboot ? status.wslFeature.detail : status.virt.detail)).c_str());
        postLog(L"⚠ WSL/VirtualMachinePlatform 已启用但组件尚未生效，请重启 Windows 后重试");
        PostMessageW(hwnd, WM_USER_ENV_INIT_COMPLETE, 2, 0);
        delete p;
        return 0;
    }

    // Step 0: 启用 Windows 功能（WSL 子系统 + 虚拟化平台）
    // 需要管理员权限，dism /enable-feature 才能执行
    if (!status.wslFeature.isEnabled) {
        postLog(L"> 启用 WSL 子系统功能...");
        auto r = utils::RunProcess(L"dism.exe",
            L"/online /enable-feature /featurename:Microsoft-Windows-Subsystem-Linux /all /norestart",
            "", 120000);
        // 3010 = 需要重启，也算成功；即使 exit=0，也要确认组件已落地
        if (r.exitCode == 0 || r.exitCode == 3010) {
            if (r.exitCode == 3010 ||
                featurePendingReboot(L"Microsoft-Windows-Subsystem-Linux") ||
                !wslsetup::IsWslComponentInstalled()) {
                rebootRequired = true;
                postLog(L"⟳ WSL 子系统功能已启用，需要重启 Windows 后组件才会生效");
            } else {
                postLog(L"✓ WSL 子系统功能已启用");
            }
        } else {
            postLog((L"✗ 启用 WSL 子系统失败（请以管理员身份运行）: " +
                     utils::Utf8ToWString(r.stdOut)).c_str());
            allOk = false;
        }
    } else {
        postLog(L"✓ WSL 子系统功能已就绪，跳过");
    }

    if (!status.virt.isEnabled) {
        postLog(L"> 启用虚拟化平台功能...");
        auto r = utils::RunProcess(L"dism.exe",
            L"/online /enable-feature /featurename:VirtualMachinePlatform /all /norestart",
            "", 120000);
        if (r.exitCode == 0 || r.exitCode == 3010) {
            if (r.exitCode == 3010 ||
                featurePendingReboot(L"VirtualMachinePlatform") ||
                !wslsetup::IsVirtualMachinePlatformComponentInstalled()) {
                rebootRequired = true;
                postLog(L"⟳ 虚拟化平台功能已启用，需要重启 Windows 后组件才会生效");
            } else {
                postLog(L"✓ 虚拟化平台功能已启用");
            }
        } else {
            postLog((L"✗ 启用虚拟化平台失败（请以管理员身份运行）: " +
                     utils::Utf8ToWString(r.stdOut)).c_str());
            allOk = false;
        }
    } else {
        postLog(L"✓ 虚拟化平台已就绪，跳过");
    }

    // 组件未落地前，安装内核/导入 Alpine 必然失败，先让用户重启
    if (rebootRequired) {
        postLog(L"⚠ 请重启 Windows 后重新运行本工具并再次点击「环境初始化」");
        PostMessageW(hwnd, WM_USER_ENV_INIT_COMPLETE, 2, 0);
        delete p;
        return 0;
    }

    // Step 1: 安装 WSL（新版安装包已内置足够新的内核）
    if (!status.wsl.isReady()) {
        postLog(L"> 安装 WSL2 内核...");
        std::string err;
        bool msiNeedsReboot = false;
        bool ok = wslsetup::InstallMsi(p->wslMsiPath, &err, &msiNeedsReboot);
        if (ok) {
            if (msiNeedsReboot) {
                rebootRequired = true;
                postLog(L"⟳ WSL2 内核安装成功，需要重启 Windows 后生效");
            } else {
                postLog(L"✓ WSL2 内核安装成功");
            }
        } else {
            postLog((L"✗ WSL2 内核安装失败: " + utils::Utf8ToWString(err)).c_str());
            allOk = false;
            if (msiNeedsReboot) rebootRequired = true;
        }
    } else {
        postLog((L"✓ WSL2 内核已就绪，跳过（" +
                 utils::Utf8ToWString(status.wsl.detail) + L"）").c_str());
    }

    if (rebootRequired) {
        postLog(L"⚠ 请重启 Windows 后重新运行本工具并再次点击「环境初始化」");
        PostMessageW(hwnd, WM_USER_ENV_INIT_COMPLETE, 2, 0);
        delete p;
        return 0;
    }

    // Step 2: 导入 Alpine
    if (!status.alpine.isReady()) {
        postLog(L"> 导入 Alpine 发行版...");
        std::string err;
        bool ok = wslsetup::ImportAlpineTar(p->alpineTarPath, &err);
        if (ok) {
            postLog(L"✓ Alpine 导入成功");
        } else {
            postLog((L"✗ Alpine 导入失败: " + utils::Utf8ToWString(err)).c_str());
            allOk = false;
        }
    } else {
        postLog(L"✓ Alpine 已就绪，跳过");
    }

    // Step 3: 安装 usbipd
    if (!status.usbipd.isReady()) {
        postLog(L"> 安装 usbipd-win...");
        std::string err;
        bool ok = wslsetup::InstallUsbipdMsi(p->usbipdMsiPath, &err);
        if (ok) {
            postLog(L"✓ usbipd-win 安装成功");
            // MSI installed, refresh PATH for current process
            wchar_t progFiles[MAX_PATH];
            if (GetEnvironmentVariableW(L"ProgramFiles", progFiles, MAX_PATH)) {
                std::wstring searchDir = std::wstring(progFiles) + L"\\usbipd-win*";
                WIN32_FIND_DATAW ffd;
                HANDLE hFind = FindFirstFileW(searchDir.c_str(), &ffd);
                if (hFind != INVALID_HANDLE_VALUE) {
                    std::wstring usbipdDir = std::wstring(progFiles) + L"\\" + ffd.cFileName;
                    FindClose(hFind);
                    std::wstring usbipdExe = usbipdDir + L"\\usbipd.exe";
                    if (GetFileAttributesW(usbipdExe.c_str()) != INVALID_FILE_ATTRIBUTES) {
                        wchar_t oldPath[4096];
                        DWORD len = GetEnvironmentVariableW(L"PATH", oldPath, 4096);
                        std::wstring newPath = usbipdDir + L";" + (len ? oldPath : L"");
                        SetEnvironmentVariableW(L"PATH", newPath.c_str());
                        postLog((L"added " + usbipdDir + L" to PATH").c_str());
                    }
                }
            }
        } else {
            postLog((L"✗ usbipd-win 安装失败: " + utils::Utf8ToWString(err)).c_str());
            allOk = false;
        }
    } else {
        postLog(L"✓ usbipd 已就绪，跳过");
    }

    // 设置 WSL 默认版本为 2（首次调用可能触发 VM 冷启动）
    if (allOk) {
        postLog(L"> 设置 WSL 默认版本...");
        auto r = utils::RunProcess(L"wsl.exe", L"--set-default-version 2", "", 60000);
        if (r.exitCode == (DWORD)-1)
            utils::RunProcess(L"wsl.exe", L"--set-default-version 2", "", 60000);
    }

    PostMessageW(hwnd, WM_USER_ENV_INIT_COMPLETE,
                 rebootRequired ? 2 : (allOk ? 1 : 0), 0);
    delete p;
    return 0;
}
// ─── Entry point ──────────────────────────────────────────────────────────

static bool IsRunningAsAdmin() {
    HANDLE hToken = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;
    TOKEN_ELEVATION elevation = {};
    DWORD size = sizeof(elevation);
    BOOL ok = GetTokenInformation(hToken, TokenElevation, &elevation, size, &size);
    CloseHandle(hToken);
    return ok && elevation.TokenIsElevated != 0;
}

static void RestartAsAdmin(HINSTANCE hInstance) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(hInstance, path, MAX_PATH);
    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = path;
    sei.nShow = SW_SHOWNORMAL;
    ShellExecuteExW(&sei);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    wchar_t logPath[MAX_PATH];
    GetModuleFileNameW(hInstance, logPath, MAX_PATH);
    std::wstring logFile(logPath);
    size_t dot = logFile.rfind(L'.');
    if (dot != std::wstring::npos) logFile.replace(dot, 4, L".log");
    else logFile += L".log";
    Logger::Instance().EnableFileLogging(logFile);

    if (!wslsetup::IsWindowsVersionSupported()) {
        DWORD build = wslsetup::GetWindowsBuildNumber();
        std::wstring msg = L"当前 Windows 版本过低（Build " + std::to_wstring(build) + L"）。\n\n"
            L"WSL 2.x 安装包要求 Windows 10 2004（Build 19041）或 Windows 11。\n"
            L"请升级 Windows 后重试。";
        MessageBoxW(nullptr, msg.c_str(), L"Windows 版本不支持", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (!IsRunningAsAdmin()) {
        int ret = MessageBoxW(nullptr,
            L"Ext4WslHelper 需要通过 usbipd 操作 USB 设备，需要管理员权限。\n\n"
            L"是否以管理员身份重新启动？",
            L"管理员权限 required",
            MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON1);
        if (ret == IDYES) {
            RestartAsAdmin(hInstance);
            return 0;
        }
    }

    MainWindow wnd(hInstance);
    if (!wnd.Create()) {
        MessageBoxW(nullptr, L"窗口创建失败", L"Ext4WslHelper", MB_ICONERROR);
        return 1;
    }
    return wnd.Run();
}
