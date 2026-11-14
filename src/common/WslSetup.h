#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

namespace wslsetup {

// Step 0: Windows 版本检查
DWORD GetWindowsBuildNumber();
bool IsWindowsVersionSupported();  // x64≥18362, ARM64≥19041

// Step 1: Windows 可选功能
bool IsWindowsFeatureEnabled(const std::wstring& featureName);

// dism 只反映“标志位是否写入”，不代表组件已经落地。
// 以下检查用于确认功能组件是否真正可用，以及是否还在等待重启。
bool IsWslComponentInstalled();                     // System32\lxss 或 LxssManager 服务
bool IsVirtualMachinePlatformComponentInstalled();  // vmcompute.exe 或 vmcompute 服务
bool IsRebootPending();                             // CBS / Windows Update / PendingFileRenameOperations

// ── 离线包查找 ─────────────────────────────────────────────────────
// 查找新版 WSL 安装包（wsl.<version>.x64.msi / arm64.msi）。
// 旧版 wsl_update_x64.msi（内核 5.10.16）内核过旧，不满足 usbipd 要求，故排除。
std::wstring FindWslPackage(const std::wstring& exeDir);

// ── 安装 ──────────────────────────────────────────────────────────

// 安装 MSI。3010（需要重启）视为成功，并通过 outNeedsReboot 告知调用方。
bool InstallMsi(const std::wstring& msiPath, std::string* outError = nullptr,
                bool* outNeedsReboot = nullptr);

// ── Alpine 发行版离线导入 ──────────────────────────────────────
//  通过 wsl --import 从 .tar/.zip 文件导入 Alpine 发行版
//  例: wsl --import Alpine %LOCALAPPDATA%\Alpine D:\path\to\alpine.tar --version 2
bool ImportAlpineTar(const std::wstring& path, std::string* outError = nullptr);

// 离线安装 usbipd-win MSI
bool InstallUsbipdMsi(const std::wstring& msiPath, std::string* outError = nullptr);

// 从 exe 所在目录查找离线安装包
std::wstring FindPackageFile(const std::wstring& exeDir, const std::wstring& pattern);

// 常量
constexpr DWORD MIN_BUILD_X64   = 18362;   // 1903
constexpr DWORD MIN_BUILD_ARM64 = 19041;    // 2004

// usbipd 要求的最低 WSL2 内核版本
constexpr const char* WSL_MIN_KERNEL_VERSION = "5.10.60.1";
// 最低 WSL 版本（2.x）
constexpr const char* WSL_MIN_VERSION = "2.0.0.0";

} // namespace wslsetup
