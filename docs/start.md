# Ext4WslHelper 启动阶段业务流程

## 环境依赖

本工具依赖以下环境组件，通过离线安装包部署：

| 组件 | 用途 | 离线包格式 |
|---|---|---|
| Windows 可选功能（WSL + 虚拟机平台） | 基础虚拟化支持 | 系统内置（dism 启用） |
| WSL2 内核 | WSL2 运行环境 | `wsl_update_x64.msi` |
| Alpine WSL 发行版 | EXT4 挂载操作环境 | `Alpine.tar` 或 `Alpine.zip`（自动解压） |
| usbipd-win | USB 设备重定向到 WSL | `usbipd-win_*.msi` |

离线安装包放置位置：exe 同目录，工具通过 `wslsetup::FindPackageFile()` 自动搜索。

---

## 整体流程

```
启动（管理员权限）
  │
  ├─ 初始化：PowershellExecutor（懒启动，首次 Execute 时创建长驻进程）
  │
  ├─ Step 0: Windows 版本检查
  │      RtlGetVersion → dwBuildNumber
  │      x64 < 18362 → ❌ 提示不支持，退出
  │      ARM64 < 19041 → ❌ 提示不支持，退出
  │
  ├─ Step 1: 环境检测（异步，后台线程，不阻塞 UI）
  │      EnvironmentService::CheckAll() → EnvironmentStatus
  │        ├── 虚拟化使能检测（dism /get-featureinfo VirtualMachinePlatform）
  │        ├── WSL 功能使能检测（dism /get-featureinfo Microsoft-Windows-Subsystem-Linux）
  │        ├── WSL 内核检测（wsl --version → 解析 WSL version / Kernel version）
  │        ├── Alpine 检测（wsl --list --verbose → 查找 Alpine 行）
  │        └── usbipd 检测（usbipd.exe → 检查退出码是否为 -1）
  │
  ├─ Step 2: 显示五项状态指示灯
  │      ● 虚拟化  ● WSL 功能  ● WSL 内核  ● Alpine  ● usbipd
  │      绿 = 就绪  黄 = 版本不满足  红 = 未安装
  │
  ├─ Step 3: 用户点击 [一键初始化] → 离线安装（后台线程）
  │      EnvInitThreadProc:
  │        ├── FindPackageFile(exeDir, "wsl*.msi")
  │        ├── FindPackageFile(exeDir, "alpine*.tar*") / "alpine*.zip"
  │        ├── FindPackageFile(exeDir, "usbipd*.msi")
  │        │
  │        ├── 安装 WSL 内核 MSI:
  │        │    msiexec /i "wsl_update_x64.msi" /quiet /norestart
  │        │    wsl --set-default-version 2
  │        │
  │        ├── 导入 Alpine 发行版:
  │        │    wsl --import Alpine <installDir> <importPath> --version 2
  │        │    wsl --set-default Alpine
  │        │    （若为 .zip 包则先 Expand-Archive 解压后导入）
  │        │
  │        └── 安装 usbipd-win MSI:
  │             msiexec /i "usbipd-win_*.msi" /quiet /norestart
  │
  └─ ✓ 全部就绪 → 启用挂载功能
```

---

## 涉及的文件操作

| 操作 | 来源 | 目标 |
|---|---|---|
| wsl_update_x64.msi 安装 | exe 同目录 | 系统级别 |
| Alpine.tar(.zip) 导入 | exe 同目录 → 解压（zip）→ `wsl --import` | `%LOCALAPPDATA%\Alpine` |
| usbipd-win_*.msi 安装 | exe 同目录 | 系统级别 (`%PROGRAMFILES%\usbipd-win`) |

## Windows 版本要求

| 架构 | 最低 Build | 对应版本 |
|------|-----------|----------|
| x64 | 18362 | 1903 |
| ARM64 | 19041 | 2004 |

## 关键代码结构

### `src/common/WslSetup.h` — 离线安装工具函数

```cpp
namespace wslsetup {

// 获取 Windows Build Number（RtlGetVersion，不受 manifest 影响）
DWORD GetWindowsBuildNumber();

// 检查 Windows 版本是否支持 WSL2
bool IsWindowsVersionSupported();

// 检查 Windows 可选功能是否启用（如 VirtualMachinePlatform）
bool IsWindowsFeatureEnabled(const std::wstring& featureName);

// 安装 MSI 包（用于 wsl_update 和 usbipd-win）
bool InstallMsi(const std::wstring& msiPath, std::string* outError = nullptr);

// 导入 Alpine tar 包为 WSL 发行版
// 支持 .tar / .tar.gz / .zip（自动解压后导入）
bool ImportAlpineTar(const std::wstring& tarPath, std::string* outError = nullptr);

// 安装 usbipd-win MSI（对 InstallMsi 的包装）
bool InstallUsbipdMsi(const std::wstring& msiPath, std::string* outError = nullptr);

// 在 exe 同目录搜索匹配的离线安装包（通配符模式）
std::wstring FindPackageFile(const std::wstring& exeDir, const std::wstring& pattern);

// 常量
constexpr DWORD MIN_BUILD_X64   = 18362;   // 1903
constexpr DWORD MIN_BUILD_ARM64 = 19041;   // 2004

}
```

### `src/services/EnvironmentService.h` — 环境检测服务

```cpp
// 五项环境检测结果
struct EnvironmentStatus {
    VirtCheckResult      virt;         // 虚拟化使能
    WslFeatureCheckResult wslFeature;  // WSL 功能使能
    WslCheckResult       wsl;          // WSL 内核
    AlpineCheckResult    alpine;       // Alpine 发行版
    UsbipdCheckResult    usbipd;       // usbipd-win

    bool allReady() const;  // 五项全部就绪
};

class EnvironmentService : public IEnvironmentService {
public:
    explicit EnvironmentService(ICommandExecutor* executor);
    EnvironmentStatus CheckAll() override;

private:
    VirtCheckResult      CheckVirtualization();  // dism /get-featureinfo VMP
    WslFeatureCheckResult CheckWslFeature();     // dism /get-featureinfo WSL
    WslCheckResult       CheckWsl();             // wsl --version
    AlpineCheckResult    CheckAlpine();           // wsl --list --verbose
    UsbipdCheckResult    CheckUsbipd();           // usbipd.exe
};
```

### `src/ui/MainWindow.h` — 主窗口环境相关

```cpp
// 控件
EnvStatusRow envStatus_[5];  // 五个状态指示灯（icon + text）
HWND envInitButton_;         // [一键初始化] 按钮
HWND detectButton_;          // [检测设备] 按钮

// 消息
#define WM_USER_ENV_CHECK_COMPLETE   (WM_USER + 20)
#define WM_USER_ENV_INIT_COMPLETE    (WM_USER + 21)

// 后台线程
struct EnvInitThreadParam {
    MainWindow* wnd;
    std::wstring wslMsiPath;
    std::wstring alpineTarPath;
    std::wstring usbipdMsiPath;
};
static DWORD WINAPI EnvInitThreadProc(LPVOID param);

// 方法
void RunEnvCheck();                              // 启动环境检测
void OnEnvCheckComplete(EnvironmentStatus*);      // 检测完成回调
void OnEnvInit();                                // [一键初始化] 处理
```

### 修改说明

- `EnvironmentService` 替代了旧的分散检测逻辑，统一返回五项结构化结果
- `WslSetup` 提供了离线安装的原子操作（InstallMsi、ImportAlpineTar、FindPackageFile）
- MainWindow 通过 `EnvInitThreadProc` 后台线程执行安装，不阻塞 UI
- 用户可通过 `envStatus_[5]` 指示灯直观了解每项的就绪状态

## 用户体验提示语

| 步骤 | 指示灯 | 日志 |
|------|--------|------|
| 版本检查失败 | — | Windows {build} < {required}，不支持 WSL2 |
| 虚拟化未启用 | ● 红 → 虚拟化 | 虚拟化未启用（需进 BIOS 开启 VT-x/AMD-V） |
| WSL 功能未启用 | ● 红 → WSL 功能 | WSL 功能未启用（需管理员运行 dism） |
| WSL 未安装 | ● 红 → WSL 内核 | WSL 未安装（点击一键初始化） |
| Alpine 未安装 | ● 红 → Alpine | Alpine 未安装（点击一键初始化） |
| usbipd 未安装 | ● 红 → usbipd | usbipd-win 未安装（点击一键初始化） |
| 安装 WSL 内核 | ● 黄 → WSL | ──── 正在安装 WSL 内核... |
| 导入 Alpine | ● 黄 → Alpine | ──── 正在导入 Alpine 发行版... |
| 安装 usbipd | ● 黄 → usbipd | ──── 正在安装 usbipd-win... |
| 全部就绪 | 五项 ● 绿 | ✓ 所有环境组件已就绪 |
