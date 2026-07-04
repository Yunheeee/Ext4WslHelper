#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include "../command/PowershellExecutor.h"
#include "../core/UsbipdManager.h"
#include "../core/WslManager.h"
#include "../core/DeviceManager.h"
#include "../core/MountManager.h"
#include "../services/EnvironmentService.h"

class MainWindow {
public:
    MainWindow(HINSTANCE hInstance);
    ~MainWindow();

    bool Create();
    int Run();
    HWND Window() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void OnDestroy();

    // 设备
    void RefreshDeviceList();
    void UpdateButtonState();
    void UpdateMountInfo();

    // 挂载 / 卸载（自动使用 Alpine 发行版）
    void OnMount();
    void OnUnmount();
    void OnToggleLog();
    void OnDeviceChange();
    void OnDetectDevice();
    void CancelDetect();

    // 环境检测
    void RunEnvCheck();
    void OnEnvCheckComplete(EnvironmentStatus* status);

    // 环境初始化（离线安装全部组件）
    void OnEnvInit();

    // 线程参数
    struct MountThreadParam {
        MainWindow* wnd;
        std::string busId;
        std::string password;
    };
    struct EnvInitThreadParam {
        MainWindow* wnd;
        // 用户指定的离线包路径（空=使用 exe 同目录自动查找）
        std::wstring wslMsiPath;
        std::wstring alpineTarPath;
        std::wstring usbipdMsiPath;
    };

    static DWORD WINAPI MountThreadProc(LPVOID param);
    static DWORD WINAPI UnmountThreadProc(LPVOID param);
    static DWORD WINAPI EnvInitThreadProc(LPVOID param);

    void AppendLog(const wchar_t* text);
    void SetMountStatus(const wchar_t* text, bool isError = false);
    void EnableControls(bool enable);

    HINSTANCE hInstance_;
    HWND hwnd_;

    // 控件
    HWND deviceCombo_;
    HWND detectButton_;
    HWND logEdit_;
    HWND toggleLogButton_;
    HWND exitButton_;

    // 环境检测指示灯（5 项）
    struct EnvStatusRow { HWND icon; HWND text; };
    EnvStatusRow envStatus_[5];  // 0=virt, 1=wslFeature, 2=wsl, 3=alpine, 4=usbipd

    // 环境操作
    HWND envInitButton_;

    // 挂载操作
    HWND windowsAccessEdit_;
    HWND mountStatusText_;
    HWND mountButton_;
    HWND unmountButton_;

    // Managers 和服务
    PowershellExecutor psExec_;
    UsbipdManager usbipdMgr_;
    WslManager wslMgr_;
    DeviceManager devMgr_;
    MountManager mountMgr_;
    EnvironmentService envService_;

    // GDI
    HFONT hFont_;
    HFONT hMonoFont_;
    HBRUSH hBrushGreen_;
    HBRUSH hBrushYellow_;
    HBRUSH hBrushRed_;

    // 状态
    bool logExpanded_;
    bool isWorking_;
    bool isDetecting_;

    // 缓存数据
    std::vector<UsbipdDeviceInfo> currentDevices_;
    std::vector<std::string> detectInitialBusIds_;
};

// 跨线程通信消息
#define WM_USER_MOUNT_COMPLETE       (WM_USER + 10)
#define WM_USER_UNMOUNT_COMPLETE     (WM_USER + 11)
#define WM_USER_LOG_APPEND           (WM_USER + 12)
#define WM_USER_ENV_CHECK_COMPLETE   (WM_USER + 20)
#define WM_USER_ENV_INIT_COMPLETE    (WM_USER + 21)
