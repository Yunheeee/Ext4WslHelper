# 组件交互与数据流设计

> 目标：理清各 `.cpp` 对外暴露的接口、组件间传递的数据结构、调用流程，用于评审设计合理性。

---

## 一、全局依赖关系图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  MainWindow (ui/MainWindow)                                                │
│  ┌──────────────────────────────────────────────────────────────────────┐   │
│  │ 持有: psExec_ (PowershellExecutor)                                   │   │
│  │      usbipdMgr_ wslMgr_ devMgr_ mountMgr_ envService_               │   │
│  └──────┬─────────┬──────────┬──────────┬──────────┬──────────┐         │
│         │         │          │          │          │          │         │
│         ▼         ▼          ▼          ▼          ▼          ▼         │
│    mountMgr_   usbipdMgr_  wslMgr_   devMgr_   envService_  psExec_    │
│    (MountMgr)  (UsbipdMgr) (WslMgr)  (DevMgr)  (EnvSvc)    (PS Exec)  │
│         │         │          │          │          │                    │
│         └──┬──────┴──────────┴──────────┘          │                    │
│            │                                       │                    │
│            │                    ┌──────────────────┘                    │
│            │                    │                                       │
│            │                    ▼                                       │
│            │              CommandWindow (内部)                          │
│            │              (进程/管道/编码/行日志)                       │
│            │                    ↑                                       │
│            │                    │ (长驻进程+标记协议)                    │
│            │            PowershellExecutor                              │
│            │                                                           │
│            │  各 Manager 直接注入 psExec_（无装饰器包裹）               │
│            │  ——命令通过 Logger 集中记录，无独立回显窗口                │
└────────────┼───────────────────────────────────────────────────────────┘
```

### 说明

- **`PowershellExecutor`** 是唯一的命令执行实现；`WslExecutor` 在项目中定义但当前 MainWindow 未使用（所有命令通过 PowerShell 执行，PowerShell 内部再启动 wsl.exe）
- **无 `CommandEchoDecorator` 装饰器**：当前 MainWindow 直接传递 `psExec_` 给各 Manager
- **无独立回显窗口**：所有命令输出通过 `Logger` 记录到日志缓冲区，再由 `AppendLog` 显示到日志编辑框
- **`EnvironmentService`** 通过 `psExec_` 执行五项环境检测

---

## 二、核心数据结构（组件间传递的"信使"）

### 2.1 `CommandResult` — 命令执行结果

```cpp
// 定义在 ICommandExecutor.h
struct CommandResult {
    DWORD exitCode;      // 进程退出码，-1 表示超时
    std::string stdOut;  // stdout 内容（UTF-8）
    std::string stdErr;  // stderr 内容（UTF-8）
};
```

**流向**：`Executor → 各 Manager → 调用方`

### 2.2 `UsbipdDeviceInfo` — USB 设备描述

```cpp
// 定义在 UsbipdManager.h
struct UsbipdDeviceInfo {
    std::string busId;       // "5-2"  — 设备标识
    std::string vidPid;      // "1234:5678"
    std::string description; // "USB Mass Storage Device"
    std::string state;       // "Not attached" / "Attached - Alpine"
    std::string wslDistro;   // 从 state 解析出的 WSL 发行版
    bool IsAttached() const;
};
```

**流向**：`UsbipdManager::List() → MainWindow`（填充设备下拉框）

### 2.3 `WslDistribution` — WSL 发行版描述

```cpp
// 定义在 WslManager.h
struct WslDistribution {
    std::string name;     // "Alpine"
    bool isDefault;       // 是否为默认发行版
    bool isRunning;       // 是否正在运行
    std::string version;  // "1" 或 "2"
    bool IsWsl2() const;
};
```

**流向**：`WslManager::ListDistributions() → MainWindow`（填充发行版下拉框）

### 2.4 `MountSession` — 挂载会话描述

```cpp
// 定义在 MountManager.h
struct MountSession {
    std::string busId;         // usbipd 总线 ID
    std::string distro;        // WSL 发行版
    std::string device;        // "sda1" — WSL 内块设备名
    std::string mountPoint;    // "/mnt/sdcard"
    std::string windowsPath;   // "\\\\wsl.localhost\\Alpine\\mnt\\sdcard"
    std::string errorDetail;   // 挂载失败时的错误描述
    bool isMounted;            // 是否已挂载
    bool isAuthError;          // 是否为 sudo 密码错误

    enum class Status { Idle, Attaching, Mounted, Disconnecting, Error };
    Status status;
};
```

**流向**：`MountManager → MainWindow`（通过回调 + 返回值通知挂载结果）

### 2.5 `UsbDeviceInfo` — 设备别名

```cpp
// 定义在 DeviceManager.h
using UsbDeviceInfo = UsbipdDeviceInfo;  // 与 UsbipdDeviceInfo 同构
```

**流向**：`DeviceManager → MainWindow`（热插拔变化回调）

### 2.6 `EnvironmentStatus` — 环境检测结果

```cpp
// 定义在 EnvironmentService.h
struct EnvironmentStatus {
    VirtCheckResult      virt;         // 虚拟化使能
    WslFeatureCheckResult wslFeature;  // WSL 功能使能
    WslCheckResult       wsl;          // WSL2 内核
    AlpineCheckResult    alpine;       // Alpine 发行版
    UsbipdCheckResult    usbipd;       // usbipd-win

    bool allReady() const;  // 五项全部就绪
};
```

**流向**：`EnvironmentService → MainWindow`（五项指示灯状态更新）

---

## 三、各组件对外接口

### 3.1 `ICommandExecutor`（抽象接口）

```
属性: 无（纯虚类）
方法:
  Execute(commandLine, stdinInput="", timeoutMs=30000) → CommandResult
    └─ stdinInput 用于向子进程标准输入写入内容（sudo -S 密码验证）
  StartBackground(commandLine) → HANDLE (进程句柄)
  IsAvailable() → bool
```

### 3.2 `PowershellExecutor` (implements ICommandExecutor)

```
构造: PowershellExecutor(showWindow=false)
方法:
  Execute()   → 内部使用长驻进程 + 标记协议
  StartBackground() → 返回进程句柄
  IsAvailable()     → 执行 echo available 判断
  SetOutputSink(callback) → 注册每解码行回调（debug 窗口接收输出）

内部行为:
  EnsureStarted(): 创建 powershell.exe -NoProfile -NoExit 长驻进程（如已存在且运行中则复用）
  ExecuteWithMarkers(): 通过标记协议 (###X4W_CSTART###/###X4W_CEND###)
    → Write(wrappedCmd) 到 stdin
    → ReadAllRaw() 活动超时读取
    → 提取标记间内容 → ProcessAndLog 编码转换/行分割/日志
    → 解析退出码
  ~PowershellExecutor(): 发送 exit\n → WaitForExit(3s) → Terminate → Close
```

### 3.3 `WslExecutor` (implements ICommandExecutor)

```
构造: WslExecutor(distro, showWindow=false)
方法:
  Execute()   → 内部同 PowershellExecutor，bash 风格标记协议
  StartBackground() → 返回进程句柄
  IsAvailable()     → 执行 echo available 判断
  GetDistro()       → 返回当前发行版名
  SetPersistentWindow(CommandWindow*) → 替换为外部传入的窗口（用于外部可见控制台）

内部行为:
  EnsureStarted(): 创建 wsl.exe -d distro (或 --cd ~ -d distro) 长驻进程
  ExecuteWithMarkers(): bash 风格标记协议
    → echo "> cmd" > /dev/tty ; echo ###X4W_CSTART###
    → cmd
    → echo ###X4W_CEND###$?
  ~WslExecutor(): 发送 exit\n → WaitForExit(3s) → Terminate → Close
```

### 3.4 `CommandWindow`（进程/管道/编码封装，被 Executor 内嵌使用）

```
构造: CommandWindow()
方法:
  Create(shellPath, showWindow, createPipe) → bool
    └─ shellPath: 长驻进程路径（如 powershell.exe -NoExit / wsl.exe -d Alpine）
  WaitForExit(timeoutMs, &exitCode) → bool
  Terminate()
  Close()
  WriteLine(command) → bool
  ReadAllLines(timeoutMs) → vector<string>
  ReadAllRaw(timeoutMs) → string
  FlushRead() → string         ← 读取管道中当前所有可用数据（立即返回）
  WriteToConsole(text)         ← 写入可见控制台窗口（showWindow=true 时有效）
  IsRunning() → bool
  GetProcessId() → DWORD
  GetProcessHandle() → HANDLE
  SetLogging(bool)
  // 静态工具
  ProcessAndLog(rawData) → vector<string>

编码处理（自动）:
  StripBom / IsLikelyUtf16Le / ConvertFromUtf16Le
  TryBetterEncoding (OEM→UTF-8 启发式判定 / ANSI→UTF-8)
  StripAnsi (剥离 ANSI 转义序列)
  每行 → Logger::Info()
```

### 3.5 `CommandEchoDecorator`（当前 MainWindow 未使用）

```
构造: CommandEchoDecorator(inner: ICommandExecutor*, label="")
方法:
  Execute(cmdLine, stdinInput, timeoutMs) → CommandResult
    → Logger::Info("==== " + label + " ==== # " + cmdLine);
    → inner->Execute(cmdLine, stdinInput, timeoutMs)
  StartBackground(cmdLine) → HANDLE
    → Logger::Info("==== " + label + " ==== # " + cmdLine + " (bg)");
  IsAvailable() → inner->IsAvailable()

说明:
  纯日志装饰器，仅通过 Logger 记录命令标签和命令行。
  无 AddConsole() / EchoText() / 控制台回显能力。
  MainWindow 当前未使用此装饰器（直接传递 psExec_ 给各 Manager）。
```

### 3.6 `UsbipdManager`

```
构造: UsbipdManager(executor: ICommandExecutor*)
属性:
  state_     → GetState() → UsbipdState
  lastError_ → GetLastError() → UsbipdErrorInfo
方法:
  List()              → vector<UsbipdDeviceInfo>
  Attach(busId, distro) → bool
  Detach(busId)          → bool
  Bind(busId)            → bool
  Unbind(busId)          → bool
  GetStateString()       → string
  // 静态
  ClassifyError(result)  → UsbipdErrorInfo
  ParseDeviceLine(line)  → UsbipdDeviceInfo
```

### 3.7 `WslManager`

```
构造: WslManager(executor: ICommandExecutor*)
属性:
  distro_ → SetDistro(distro)
方法:
  ListDistributions()         → vector<WslDistribution>
  GetDefaultDistribution()    → WslDistribution
  IsExt4Supported(distro)    → bool
  GetDiskList()               → string  (cat /proc/partitions 输出的磁盘列表)
  DetectBlockDevice(previousDisks) → string  (通过 /proc/partitions 对比检测新设备)
  EnsureUsbStorageModule()    → bool    (lsmod → modprobe usb-storage)
  MountExt4(device, mountPoint, sudoPassword, &isAuthError) → string (错误描述，空=成功)
  IsMounted(mountPoint)       → bool    (mountpoint -q <path>)
  Unmount(mountPoint, sudoPassword) → bool
  // 静态
  MakeWindowsPath(distro, mountPoint) → string
```

### 3.8 `MountManager`

```
构造: MountManager(usbipd: UsbipdManager*, wsl: WslManager*)
方法:
  AttachAndMount(busId, distro, sudoPassword) → MountSession
  UnmountAndDetach(busId, sudoPassword) → bool
  ListActiveSessions() → vector<MountSession>
  GetSession(busId) → MountSession
  SetNotificationCallback(callback)
  CleanupDisconnected(enumeratePresentBusIds)
```

### 3.9 `DeviceManager`

```
构造: DeviceManager(executor: ICommandExecutor*)
属性:
  callback_ → SetChangeCallback(callback)
  newDevices_ / removedDevices_ → GetNewDevices() / GetRemovedDevices()
方法:
  EnumerateDevices()    → vector<UsbDeviceInfo>
  Refresh()             → bool  (有变化则触发 callback)
  GetPresentBusIds()    → vector<string>
```

### 3.10 `EnvironmentService`

```
构造: EnvironmentService(executor: ICommandExecutor*)
方法:
  CheckAll() → EnvironmentStatus
    └─ 内部依次调用五项检测，返回一次性结果

检测项:
  CheckVirtualization()   → VirtCheckResult      (dism 查询 VMP 功能状态)
  CheckWslFeature()       → WslFeatureCheckResult (dism 查询 WSL 功能状态)
  CheckWsl()              → WslCheckResult        (wsl --version)
  CheckAlpine()           → AlpineCheckResult     (wsl --list --verbose)
  CheckUsbipd()           → UsbipdCheckResult     (usbipd.exe)
```

### 3.11 `MainWindow`（GUI 入口，非抽象）

```
构造: MainWindow(hInstance)
方法:
  Create() → bool
  Run() → int
  Window() → HWND

内部组织（用于理解流程）:
  // 事件处理
  HandleMessage(msg, wParam, lParam)

  // UI 动作
  OnMount() / OnUnmount() / OnToggleLog()
  RefreshDeviceList() / UpdateButtonState() / UpdateMountInfo()
  OnDetectDevice() / CancelDetect()
  RunEnvCheck() / OnEnvCheckComplete() / OnEnvInit()

  // 后台线程
  MountThreadProc() / UnmountThreadProc() / EnvInitThreadProc()

  // 成员
  PowershellExecutor psExec_;
  UsbipdManager usbipdMgr_;
  WslManager wslMgr_;
  DeviceManager devMgr_;
  MountManager mountMgr_;
  EnvironmentService envService_;
  EnvStatusRow envStatus_[5];   // 五项环境检测指示灯
```

### 3.12 `Logger`（全局单例）

```
方法（静态）:
  Instance() → Logger&
  Info(msg) / Debug(msg) / Warning(msg) / Error(msg)
  EnableFileLogging(filePath)
  GetBuffer() → string   (供 UI 日志控件拉取)
  ClearBuffer()
  SetLevel(level)
```

### 3.13 `PasswordDialog`（自由函数）

```
ShowPasswordDialog(parent) → string (密码, 取消返回空)
ShowWslUserSetupDialog(parent, &outUsername, &outPassword) → bool
```

### 3.14 `CmdWinTestWindow` / `DebugTestWindow`（自由函数，仅 `_DEBUG` 模式）

```
// 仅在 _DEBUG 构建中可用（#ifdef _DEBUG）
ShowCmdWinTestWindow(parent) → CmdWinHandles
  └─ 创建独立调试窗口，接收 executor 命令输出展示

ShowDebugTestWindow(parent)
  └─ 命令执行调试器

CmdWinAppendText(hwnd, line)
  └─ 向 CmdWin 调试窗口追加带时间戳的行
```

---

## 四、核心业务流程

### 4.1 环境检测流程

```
用户启动程序 / 点击检测
    │
    ▼
MainWindow::RunEnvCheck()
    │
    └── envService_.CheckAll()
           │
           ├── CheckVirtualization()  → VirtCheckResult
           │    └── dism /online /get-featureinfo /featurename:VirtualMachinePlatform
           │
           ├── CheckWslFeature()      → WslFeatureCheckResult
           │    └── dism /online /get-featureinfo /featurename:Microsoft-Windows-Subsystem-Linux
           │
           ├── CheckWsl()             → WslCheckResult
           │    └── wsl.exe --version
           │         └── 解析 WSL version / Kernel version
           │         └── 回退: wsl.exe --list --verbose
           │
           ├── CheckAlpine()          → AlpineCheckResult
           │    └── wsl.exe --list --verbose 中查找 Alpine
           │
           └── CheckUsbipd()          → UsbipdCheckResult
                └── usbipd.exe (检查退出码是否为 -1)
        │
        ▼
    EnvironmentStatus → PostMessage(WM_USER_ENV_CHECK_COMPLETE)
        │
        ▼
    MainWindow::OnEnvCheckComplete()
        ├── 更新五项指示灯（绿/黄/红）
        ├── 全部就绪 → 启用挂载按钮
        └── 部分未就绪 → 启用 [一键初始化] 按钮
```

### 4.2 环境初始化流程（离线安装）

```
用户点击 [一键初始化]
    │
    ▼
MainWindow::OnEnvInit()
    │
    └── CreateThread(EnvInitThreadProc)
        │
        ▼
    EnvInitThreadProc()
        │
        ├── wslsetup::FindWslPackage(exeDir)
        ├── wslsetup::FindPackageFile(exeDir, "alpine*.tar*")
        ├── wslsetup::FindPackageFile(exeDir, "alpine*.zip")
        ├── wslsetup::FindPackageFile(exeDir, "usbipd*.msi")
        │
        ├── 安装 WSL MSI: wslsetup::InstallMsi(wslMsiPath)
        │    └── msiexec /i "wsl.2.2.1.0.x64.msi" /quiet /norestart
        │
        ├── 导入 Alpine:  wslsetup::ImportAlpineTar(alpinePath)
        │    ├── 若为 .zip 则先解压
        │    └── wsl --import Alpine <installDir> <importPath> --version 2
        │
        ├── 安装 usbipd: wslsetup::InstallUsbipdMsi(usbipdMsiPath)
        │    └── msiexec /i "usbipd-win*.msi" /quiet /norestart
        │
        └── PostMessage(WM_USER_ENV_INIT_COMPLETE)
```

### 4.3 挂载流程

```
用户点击 [挂载]
    │
    ▼
MainWindow::OnMount()
    │
    ├── 读取 UI: busId ← deviceCombo_, password ← 已缓存
    ├── 创建 MountThreadParam { busId, password }
    ├── CreateThread(MountThreadProc)  ← 后台线程，不阻塞 UI
    │
    ▼
MountThreadProc()
    │
    └── mountMgr_.AttachAndMount(busId, "Alpine", password)
           │
           ├── wslMgr_.SetDistro("Alpine")
           │
           ├── wslMgr_.IsMounted("/mnt/sdcard")
           │    └── 有残留挂载 → Unmount → 清理
           │
           ├── wslMgr_.GetDiskList() → disksBefore : string
           │
           ├── wslMgr_.EnsureUsbStorageModule()
           │    ├── lsmod | grep usb_storage
           │    ├── 未加载 → modprobe usb-storage 2>&1
           │    └── 失败 → depmod -a → 重试 modprobe
           │
           ├── usbipdMgr_.Bind(busId)          ← 先 bind 确保可访问
           ├── usbipdMgr_.Attach(busId, "Alpine") → bool
           │
           ├── 轮询: wslMgr_.DetectBlockDevice(disksBefore) → device : string
           │    └── 最多 20 次 × 500ms = 10s
           │    └── 超时 → Detach 回滚 → 返回 MountSession{status=Error}
           │
           ├── wslMgr_.MountExt4(device, "/mnt/sdcard", password, &isAuthError)
           │    ├── 尝试 device+"1"   (sda1)
           │    ├── 尝试 device+"p1"  (sda1p1)
           │    ├── 全失败 → Detach 回滚
           │    └── 成功 → MountSession{ isMounted=true, windowsPath=... }
           │
           ├── MountSession 入 sessions_ 列表
           ├── callback_(session) → 通知 UI
           │
           ▼
    主线程收到 WM_USER_MOUNT_COMPLETE
           │
           ├── UI 更新: 状态文本、挂载路径显示、设备列表刷新
           └── AppendLog("[MountManager] Mount complete: ...")
```

### 4.4 卸载流程

```
用户点击 [卸载]
    │
    ▼
MainWindow::OnUnmount()
    │
    └── mountMgr_.UnmountAndDetach(busId, password)
           │
           ├── 查找 session (sessions_ 列表，互斥锁保护)
           ├── wslMgr_.Unmount(mountPoint, password)  ← WSL 内 umount（循环卸载+懒惰卸载回退）
           ├── usbipdMgr_.Detach(busId)               ← 从 WSL 分离（幂等，已分离也返回成功）
           ├── 从 sessions_ 中移除
           └── callback_(session{status=Idle}) → 通知 UI
        │
        ▼
   主线程收到 WM_USER_UNMOUNT_COMPLETE → UI 刷新
```

### 4.5 设备热插拔检测

```
MainWindow::OnDeviceChange()  (轮询调用)
    │
    └── devMgr_.Refresh()
           │
           ├── 首次调用: 缓存当前设备列表，返回 false
           ├── 后续调用: 对比缓存，计算 newDevices_ / removedDevices_
           │
           └── 有变化 → callback_() → MainWindow::OnDeviceChange()
                  │
                  └── RefreshDeviceList()
                        │
                        ├── usbipdMgr_.List() → vector<UsbipdDeviceInfo>
                        ├── 更新 UI 设备下拉框
                        ├── 若已挂载设备被移除 → CleanupDisconnected()
                        └── UpdateButtonState()
```

### 4.6 命令执行流程（长驻进程 + 标记协议）

```
Executor::Execute(cmdLine)     (showWindow=false)
    │
    ├── EnsureStarted()          ← 创建/复用长驻进程
    │    ├── window_ 存在且运行中 → 直接返回
    │    └── 否则 → CommandWindow::Create(powershell.exe -NoProfile -NoExit)
    │
    ├── ExecuteWithMarkers()
    │    │
    │    ├── 构建包裹命令 (PS 风格):
    │    │    try{WriteAllText('CONOUT$', '> cmd')}catch{}
    │    │    ###X4W_CSTART###
    │    │    <实际命令>
    │    │    $r=$LASTEXITCODE; Write-Host '###X4W_CEND###'$r
    │    │
    │    ├── CommandWindow::WriteLine(wrappedCmd)  ← 写入 stdin
    │    ├── if stdinInput → WriteLine(stdinInput) ← sudo 密码等
    │    │
    │    ├── CommandWindow::ReadAllRaw(timeoutMs) → string
    │    │    └── 活动超时轮询: 有数据则重置计时, 无数据则 Sleep(50)
    │    │
    │    ├── 提取 ###X4W_CSTART### ~ ###X4W_CEND### 间内容
    │    │
    │    ├── CommandWindow::ProcessAndLog(content)
    │    │    ├── StripBom / IsLikelyUtf16Le / ConvertFromUtf16Le
    │    │    ├── TryBetterEncoding (OEM/UTF-8 启发式判定/ANSI→UTF-8)
    │    │    ├── StripAnsi (剥离 ANSI 转义序列)
    │    │    ├── 每行 → Logger::Info() + 加入 lines[]
    │    │    └── return lines
    │    │
    │    ├── 解析 CEND 后的退出码
    │    │
    │    └── → CommandResult{ exitCode, stdOut, stdErr }
```

---

## 五、组件间依赖注入关系

```
MainWindow 构造函数中建立依赖链:

  psExec_       = PowershellExecutor(false)  ← 非 showWindow 模式

  // 各 Manager 直接注入 psExec_（无装饰器包裹）
  // 命令输出通过 Logger::Info() 集中记录，无需独立回显窗口

  usbipdMgr_    = UsbipdManager(&psExec_)
  wslMgr_       = WslManager(&psExec_)
  devMgr_       = DeviceManager(&psExec_)
  envService_   = EnvironmentService(&psExec_)

  mountMgr_     = MountManager(&usbipdMgr_, &wslMgr_)
  mountMgr_.SetNotificationCallback(...)     ← 注册回调
```

---

## 六、关键设计决策（供评审）

### 6.1 为什么 core 层通过 `ICommandExecutor*` 注入？

```
core/UsbipdManager          core/WslManager          services/EnvironmentService
       │                            │                            │
       │ 依赖 ICommandExecutor*     │ 依赖 ICommandExecutor*     │ 依赖 ICommandExecutor*
       ▼                            ▼                            ▼
   PowershellExecutor          PowershellExecutor          PowershellExecutor
   (长驻进程 + 标记协议)         (长驻进程 + 标记协议)         (长驻进程 + 标记协议)
   → CommandWindow 统一转 UTF-8  → CommandWindow 统一转 UTF-8  → CommandWindow 统一转 UTF-8
   → 按行 Logger                 → 按行 Logger                 → 按行 Logger
```

**好处**：
- 单元测试可 mock 接口，无需真实 WSL/usbipd 环境
- 调用方不感知底层是 PowerShell 还是 WSL
- 装饰器（`CommandEchoDecorator`）可透明插入

### 6.2 为什么使用长驻进程（而非每次创建临时进程）？

```
临时进程模式:                     长驻进程模式:
Executor::Execute()              Executor::Execute()
  │                                 │
  ├── CreateProcess                 ├── EnsureStarted (一次创建，后续复用)
  ├── 执行命令                      ├── ExecuteWithMarkers (标记协议包裹)
  ├── ReadAllLines / WaitForExit    ├── 写入 stdin → 读取 stdout
  └── Close                         └── 下次复用同一进程
```

**动机**：
- 消除每次 `Execute()` 创建进程的开销（特别在轮询检测设备时大量调用）
- 标记协议 (`###X4W_CSTART/END###`) 准确区分单次输出边界
- `CommandWindow` 统一编码处理和超时控制

### 6.3 为什么使用 Alpine 而非 Ubuntu 作为 WSL 发行版？

```
Alpine:
  • 镜像约 5MB（wsl --import 秒级完成）
  • 默认 root 用户，挂载命令无需 sudo 密码交互
  • wsl --import 命令稳定可靠
  • 离线 tar 包 + zip 包均可自动处理

Ubuntu:
  • 镜像 > 200MB，下载/导入慢
  • 需要创建非 root 用户 + sudo 密码
  • 首次启动需要交互式配置
```

### 6.4 为什么从在线下载改为离线安装？

```
在线下载流程（旧）:                 离线安装流程（当前）:
  download URL                       exe 同目录放置:
    → wsl_update_x64.msi               • wsl.2.2.1.0.x64.msi
    → Ubuntu.appx                      • Alpine.tar / .zip
    → 网络慢/失败风险大                  • usbipd-win_x64.msi
                                       → FindPackageFile 自动查找
                                       → InstallMsi / ImportAlpineTar
```

**动机**：
- 企业内网/无网络环境部署
- 避免微软 CDN URL 变化导致程序不可用
- 用户可自行准备离线包，单 exe + 离线包完成部署

### 6.5 MountManager 的回调机制

```cpp
// MountManager.h
void SetNotificationCallback(function<void(const MountSession&)>);
```

- `AttachAndMount` 和 `UnmountAndDetach` 在流程关键节点调用 `callback_(session)`
- MainWindow 通过此回调更新 UI（启用/禁用按钮、状态文本）
- 类似的还有 `DeviceManager::SetChangeCallback`

### 6.6 状态机驱动

`UsbipdManager` 内部维护 `UsbipdState` 状态机（Unknown → ServiceRunning → DeviceRecognized → DeviceBound/DeviceAttached），`MainWindow` 通过 `GetState()` / `GetStateString()` 查询来更新按钮状态和用户提示。状态自动跟随 `List()` / `Attach()` / `Detach()` / `Bind()` / `Unbind()` 操作更新。
