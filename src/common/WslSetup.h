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

// ── 安装 ──────────────────────────────────────────────────────────

bool InstallMsi(const std::wstring& msiPath, std::string* outError = nullptr);

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

} // namespace wslsetup
