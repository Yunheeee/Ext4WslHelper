#include "WslSetup.h"
#include "Utils.h"
#include "Logger.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <shellapi.h>
#include <string>

#pragma comment(lib, "shell32.lib")

namespace wslsetup {

// ─── Step 0: Windows 版本 ─────────────────────────────────────────────────

DWORD GetWindowsBuildNumber() {
    // RtlGetVersion 不受应用程序清单影响（GetVersionEx 可能返回假值）
    using RtlGetVersion_t = NTSTATUS (WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto pfn = (RtlGetVersion_t)GetProcAddress(ntdll, "RtlGetVersion");
    if (!pfn) return 0;

    RTL_OSVERSIONINFOW vi = { sizeof(vi) };
    if (pfn(&vi) != 0) return 0;
    return vi.dwBuildNumber;
}

bool IsWindowsVersionSupported() {
    DWORD build = GetWindowsBuildNumber();

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    bool isArm64 = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64);

    DWORD required = isArm64 ? MIN_BUILD_ARM64 : MIN_BUILD_X64;
    if (build < required) {
        Logger::Instance().Error("[WslSetup] Windows build " + std::to_string(build) +
                                 " < " + std::to_string(required) +
                                 (isArm64 ? " (ARM64)" : " (x64)"));
        return false;
    }
    Logger::Instance().Info("[WslSetup] Windows build " + std::to_string(build) + " OK");
    return true;
}

// ─── Step 1: Windows 功能 ─────────────────────────────────────────────────

bool IsWindowsFeatureEnabled(const std::wstring& featureName) {
    std::wstring args = L"/online /get-featureinfo /featurename:" + featureName;
    auto r = utils::RunProcess(L"dism.exe", args, "", 30000);
    // State: Enabled 或 状态: 已启用
    bool found = r.exitCode == 0 &&
               (r.stdOut.find("Enabled") != std::string::npos ||
                r.stdOut.find("\xe5\xb7\xb2\xe5\x90\xaf\xe7\x94\xa8") != std::string::npos);
    Logger::Instance().Info("[WslSetup] dism " + utils::WStringToUtf8(featureName) +
                            " enabled=" + std::to_string(found) +
                            " exit=" + std::to_string(r.exitCode));
    Logger::Instance().Debug("[WslSetup] dism " + utils::WStringToUtf8(featureName) +
                             " raw=" + r.stdOut.substr(0, 300));
    return found;
}

// ─── 安装 MSI ──────────────────────────────────────────────────────────────

bool ImportAlpineTar(const std::wstring& tarPath, std::string* outError) {
    Logger::Instance().Info("[WslSetup] Importing Alpine from: " +
                            utils::WStringToUtf8(tarPath));

    // 获取 LOCALAPPDATA 作为安装目标
    wchar_t localAppData[MAX_PATH];
    if (!GetEnvironmentVariableW(L"LOCALAPPDATA", localAppData, MAX_PATH)) {
        if (outError) *outError = "无法获取 LOCALAPPDATA 路径";
        return false;
    }
    std::wstring installDir = std::wstring(localAppData) + L"\\Alpine";

    // 创建安装目录
    CreateDirectoryW(installDir.c_str(), nullptr);

    // 判断是否为 zip 包 — 需要先解压再导入
    std::wstring importPath = tarPath;
    size_t len = tarPath.size();
    bool isZip = (len >= 4 &&
                  (tarPath.substr(len - 4) == L".zip" || tarPath.substr(len - 4) == L".ZIP"));

    if (isZip) {
        std::wstring extractDir = installDir + L"\\.tmp_extract";

        // 清空上次残留 — 注意 SHFileOperationW 需要双 null 终止符
        SHFILEOPSTRUCTW fo = {};
        fo.wFunc = FO_DELETE;
        std::wstring nullTerminated = extractDir + L"\0"; // 额外 null 使字符串双 null 终止
        fo.pFrom = nullTerminated.c_str();
        fo.fFlags = FOF_NO_UI | FOF_SILENT;
        SHFileOperationW(&fo);

        Logger::Instance().Info("[WslSetup] Extracting zip: " +
                                utils::WStringToUtf8(tarPath));

        // 用 PowerShell Expand-Archive 解压
        std::wstring psCmd = L"Expand-Archive -Path '" + tarPath +
                             L"' -DestinationPath '" + extractDir + L"' -Force";
        auto extract = utils::RunProcess(L"powershell.exe",
            L"-NoProfile -Command \"" + psCmd + L"\"", "", 60000);

        if (extract.exitCode != 0) {
            std::string err = "解压 Alpine.zip 失败: " + extract.stdOut + " " + extract.stdErr;
            Logger::Instance().Error("[WslSetup] " + err);
            if (outError) *outError = err;
            return false;
        }

        // 在解压目录中查找 .tar / .tar.gz 文件
        WIN32_FIND_DATAW ffd;
        std::wstring searchPattern = extractDir + L"\\*.tar*";
        HANDLE hFind = FindFirstFileW(searchPattern.c_str(), &ffd);
        if (hFind != INVALID_HANDLE_VALUE) {
            // 找到 tar → 用 tar 路径导入
            importPath = extractDir + L"\\" + ffd.cFileName;
            FindClose(hFind);
        } else {
            // 无嵌套 tar → 尝试直接导入目录（zip 内含 rootfs 目录结构）
            importPath = extractDir;
        }
    }

    // wsl --import Alpine <installDir> <importPath> --version 2
    std::wstring args = L"--import Alpine \"" + installDir + L"\" \"" +
                        importPath + L"\" --version 2";

    auto r = utils::RunProcess(L"wsl.exe", args, "", 120000);

    if (r.exitCode != 0) {
        std::string err = r.stdOut + " " + r.stdErr;
        Logger::Instance().Error("[WslSetup] wsl --import Alpine failed: " + err);
        if (outError) *outError = err;
        return false;
    }

    // 设置 Alpine 为默认发行版
    utils::RunProcess(L"wsl.exe", L"--set-default Alpine", "", 10000);

    Logger::Instance().Info("[WslSetup] Alpine imported successfully");
    return true;
}

// ─── 安装 MSI ──────────────────────────────────────────────────────────────

bool InstallMsi(const std::wstring& msiPath, std::string* outError) {
    Logger::Instance().Info("[WslSetup] Installing MSI...");
    std::wstring args = L"/i \"" + msiPath + L"\" /quiet /norestart";
    auto r = utils::RunProcess(L"msiexec.exe", args, "", 300000);
    Logger::Instance().Info("[WslSetup] msiexec exit=" + std::to_string(r.exitCode));
    if (r.exitCode != 0 && outError) {
        *outError = r.stdErr.empty() ? r.stdOut : r.stdErr;
    }
    return r.exitCode == 0;
}

// ─── 安装 usbipd-win MSI（离线安装包）────────────────────────────────────

bool InstallUsbipdMsi(const std::wstring& msiPath, std::string* outError) {
    Logger::Instance().Info("[WslSetup] Installing usbipd from: " +
                            utils::WStringToUtf8(msiPath));
    return InstallMsi(msiPath, outError);
}

// ─── 在 exe 目录下搜索离线安装包 ─────────────────────────────────────────

std::wstring FindPackageFile(const std::wstring& exeDir, const std::wstring& pattern) {
    std::wstring fullPattern = exeDir + L"\\" + pattern;
    WIN32_FIND_DATAW ffd;
    HANDLE hFind = FindFirstFileW(fullPattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return L"";

    std::wstring result = exeDir + L"\\" + ffd.cFileName;
    FindClose(hFind);
    return result;
}

} // namespace wslsetup
