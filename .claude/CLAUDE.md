# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Windows GUI 工具，在 Windows 上访问 EXT4 文件系统格式的 SD 卡。技术原理基于 WSL2：

1. 通过 `usbipd` 将 EXT4 格式 SD 卡从 Windows 侧 attach 到 WSL2 虚拟机
2. 在 WSL2 内挂载 EXT4 分区
3. 通过 `\\wsl.localhost\<distro>\` 网络路径在 Windows 端访问

**当前设计目标**：离线安装模式（用户准备 Alpine.tar + usbipd-win.msi + wsl.msi 安装包，放在 exe 同目录），不用在线下载。

## Build System

- **IDE**: Visual Studio 2022 (VS2022)
- **项目文件**: `.vcxproj` / `.sln` 方案（VS2022 原生）
- **输出**: 单文件免安装 `.exe`，静态链接运行时
- **配置**: Debug / Release, x64

### 常用命令

```bash
# 命令行构建 (x64 Release)
msbuild Ext4WslHelper.sln /p:Configuration=Release /p:Platform=x64

# 命令行构建 (x64 Debug)
msbuild Ext4WslHelper.sln /p:Configuration=Debug /p:Platform=x64

# 运行单元测试（直接运行测试 exe）
x64\Release\Ext4WslHelper.Tests.exe

# 运行单个测试
x64\Release\Ext4WslHelper.Tests.exe --gtest_filter=*TestName*
```

或通过 VS2022 IDE 直接打开 `.sln` 构建/运行测试。

## Code Architecture

```
Ext4WslHelper/
├── .github/workflows/       # CI/CD — 编译 & 发布 Release
│   └── build-release.yml    #   tag 推送触发 / workflow_dispatch 手动触发
├── src/
│   ├── ui/                   # Win32 GUI 层 — 窗口、控件、对话框
│   │   ├── MainWindow/       # 主窗口
│   │   ├── DebugTestWindow/  # 命令执行调试器（_DEBUG 模式）
│   │   ├── CmdWinTestWindow/ # 命令窗口调试器（_DEBUG 模式）
│   │   └── Dialogs/          # 设置/关于/错误对话框
│   ├── core/                 # 核心业务逻辑层 — 不依赖 Win32 API
│   │   ├── UsbipdManager/    # usbipd 的枚举、attach、detach 操作
│   │   ├── WslManager/       # WSL 发行版检测、挂载操作
│   │   ├── MountManager/     # 挂载编排: Bind→Attach→DetectDevice→Mount
│   │   └── DeviceManager/    # USB 设备枚举、热插拔差异检测
│   ├── command/              # 命令执行抽象层 — 进程操控核心模块
│   │   ├── ICommandExecutor.h # 对外统一接口（纯虚类）
│   │   ├── PowershellExecutor # PowerShell 长驻进程 + 标记协议
│   │   ├── WslExecutor        # WSL 长驻进程 + bash 标记协议
│   │   ├── CommandWindow      # 进程/管道/编码封装（被 Executor 内嵌使用）
│   │   └── CommandEchoDecorator # 日志装饰器（命令执行前记录标签）
│   ├── services/             # 服务层
│   │   └── EnvironmentService/ # 环境全量检测（虚拟化/WSL/Alpine/usbipd）
│   └── common/               # 跨模块共享
│       ├── Logger/           # 日志模块
│       ├── Utils/            # 字符串、编码转换、RunProcess 工具
│       ├── Error/            # 统一错误码/异常处理
│       └── WslSetup/         # WSL 环境离线安装（系统版本检查、MSI/Tar导入）
├── tests/
│   ├── UsbipdManagerTests.cpp
│   ├── WslManagerTests.cpp
│   ├── MountManagerTests.cpp
│   ├── DeviceManagerTests.cpp
│   ├── CommandExecutorTests.cpp
│   ├── CommandWindowTests.cpp
│   ├── UtilsTests.cpp
│   ├── MockCommandExecutor.h
│   └── IntegrationTests.cpp
├── scripts/
│   └── gen-version.ps1       # 编译前生成版本头文件（从 VERSION 文件读取）
├── resources/
│   ├── app.manifest          # 应用程序清单
│   └── app.rc                # 图标/版本资源（版本号由 version.h 宏注入）
├── docs/                     # 设计文档
├── VERSION                   # 版本号唯一来源（纯文本，如 0.0.0）
├── packages.yaml             # 离线安装包下载地址（供 CI 构建时下载）
├── README.md                 # 项目说明
└── Ext4WslHelper.sln         # VS2022 解决方案文件
```

### 分层依赖规则

```
ui → core → command (interface only)
                ↓
         [PowershellExecutor | WslExecutor] (concrete, no upstream deps)

services → core → command (interface only)
```

- `ui/` 调用 `core/` 的业务方法，不直接操作任何进程
- `core/` 各 Manager 通过 `ICommandExecutor` 接口（构造注入）执行命令，不直接创建进程
- `command/` 是底层进程操控模块：`ICommandExecutor.h` 定义纯虚接口，`PowershellExecutor` 和 `WslExecutor` 是其实现，各自内嵌 `CommandWindow` 管理单一长驻进程，通过标记协议收发命令，封装编码转换和超时控制
- `common/` 零依赖，纯工具函数和类型定义
- `services/EnvironmentService` 通过 `ICommandExecutor` 检测各项环境状态
- 测试项目独立；业务测试通过 mock `ICommandExecutor` 验证逻辑，无需真实 WSL/usbipd 环境

### ICommandExecutor 接口定义示意

```cpp
struct CommandResult {
    DWORD exitCode;
    std::string stdOut;
    std::string stdErr;
};

class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;
    // 同步执行命令，支持 stdin 输入（用于 sudo -S 密码）
    virtual CommandResult Execute(const std::string& commandLine,
                                  const std::string& stdinInput = "",
                                  DWORD timeoutMs = 30000) = 0;
    // 启动后台进程（不等待），返回进程句柄用于后续管理
    virtual HANDLE StartBackground(const std::string& commandLine) = 0;
    // 检查命令/环境是否可用
    virtual bool IsAvailable() = 0;
};
```

- `PowershellExecutor` 通过 `CommandWindow` 管理单一长驻 `powershell.exe -NoProfile -NoExit` 进程，通过标记协议 (`###X4W_CSTART/END###`) 多次收发命令
- `WslExecutor` 类似，管理 `wsl.exe -d <distro>` 长驻进程，bash 风格标记协议
- `CommandEchoDecorator` 包装 `ICommandExecutor`，在命令执行前通过 `Logger` 记录标签和命令行（仅日志，无控制台回显）
- 上层代码只依赖 `ICommandExecutor`，运行时通过工厂或 DI 注入具体实现

### 版本号管理

```
VERSION (纯文本)  ──▶  scripts/gen-version.ps1  ──▶  src/common/version.h  ──▶  resources/app.rc  ──▶  exe
      0.0.0             编译前自动执行                   生成宏 (不跟踪)              #include version.h       嵌入版本资源
```

- `VERSION` 文件是版本号的**唯一来源**，修改它即修改所有引用处
- `gen-version.ps1` 在每次 `msbuild` 编译前自动运行（vcxproj 的 `GenerateVersionHeader` target，`BeforeTargets="ResourceCompile"`）
- 生成的 `src/common/version.h` 包含 `VERSION_COMMA`（PE 四位整数）和 `VERSION_STRING`（字符串），不被 git 跟踪
- `app.rc` 通过 `#include "../src/common/version.h"` 获取版本宏，替代硬编码

### CI/CD

`.github/workflows/build-release.yml`：

| 触发方式 | 行为 |
|---|---|
| `git tag vX.Y.Z` + push | 自动编译 → 下载离线包 → 打包 `Ext4WslHelper-vX.Y.Z.zip` → 发布 GitHub Release |
| GitHub Actions 页面手动触发 | 编译 → 下载离线包 → 打包 zip（版本号取 VERSION 文件或手动填写） |

手动触发时可输入 `version` 参数覆盖 VERSION 文件。tag 触发时自动从 `v` 前缀提取版本号。zip 包内结构：

```
Ext4WslHelper-v1.0.0.zip
├── Ext4WslHelper.exe
├── README.md
└── packages/
    ├── wsl_update_x64.msi
    ├── Alpine.zip
    └── usbipd-win_5.3.0_x64.msi
```

离线包下载地址定义在 `packages.yaml` 中。

### 关键技术栈

| 模块 | 技术 |
|------|------|
| GUI 框架 | Win32 API (窗口/对话框/控件) |
| 命令执行层 | `CreateProcess` + 匿名管道 + `CommandWindow` 封装 |
| 进程会话 | 单一长驻进程（`powershell.exe` / `wsl.exe`）+ 标记协议 `###X4W_CSTART/END###` |
| PowerShell 交互 | `powershell.exe -NoProfile -NoExit` 长驻进程管道 |
| WSL 交互 | `wsl.exe -d <distro>` 长驻进程管道 |
| usbipd 交互 | `usbipd.exe` 进程调用 + 管道输出解析 |
| 挂载管理 | WSL 内 `cat /proc/partitions` / `mount` / `umount` / `modprobe` |
| 网络路径 | `\\wsl.localhost\<distro>\<mountpoint>` SMB 访问 |
| 热插拔 | `DeviceManager` 轮询对比 (`Refresh()` 快照差异) |
| 环境检测 | `EnvironmentService` 五项全量检测（虚拟化/WSL功能/WSL内核/Alpine/usbipd） |

## Design Principles

- **测试先行**: 新增功能前先写单元测试
- **模块化**: 每个模块有清晰接口，可单独编译测试
- **命令执行抽象**: 所有外部进程调用通过 `ICommandExecutor` 接口，core 层业务逻辑通过构造函数注入该接口，单元测试传入 mock 实现即可验证业务逻辑
- **错误处理**: 统一错误码，所有外部进程失败给出用户可理解的中文提示
- **免安装**: 静态链接 CRT，单 exe 分发，不依赖额外 DLL
- **离线安装**: 环境初始化通过 exe 同目录的离线包（Alpine.tar、wsl.msi、usbipd.msi）完成，不依赖网络下载
- **长驻进程**: Executor 内部维护单一长驻 shell 进程，通过标记协议多次收发命令，消除每次创建进程的开销
