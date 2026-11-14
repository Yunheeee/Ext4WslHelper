#include "WslSetup.h"
#include "Utils.h"
#include "Logger.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsvc.h>
#include <winternl.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cwctype>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace wslsetup {

namespace {

std::string ToLowerAscii(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

std::wstring ToLowerW(const std::wstring& s) {
    std::wstring out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return (wchar_t)towlower(c); });
    return out;
}

bool PathExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool ServiceExists(const std::wstring& serviceName) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, serviceName.c_str(), SERVICE_QUERY_STATUS);
    bool exists = (svc != nullptr);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return exists;
}

bool RegKeyExists(HKEY root, const wchar_t* subKey) {
    HKEY key = nullptr;
    LONG rc = RegOpenKeyExW(root, subKey, 0, KEY_QUERY_VALUE, &key);
    if (key) RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

std::wstring System32Path(const wchar_t* leaf) {
    wchar_t sysDir[MAX_PATH] = {};
    UINT len = GetSystemDirectoryW(sysDir, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return {};
    std::wstring path(sysDir, len);
    path += L"\\";
    path += leaf;
    return path;
}

// 读取 MSI 日志（可能是 ANSI/OEM 或 UTF-16），返回 UTF-8 文本
std::string ReadMsiLog(const std::wstring& logPath) {
    HANDLE h = CreateFileW(logPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER fileSize = {};
    if (!GetFileSizeEx(h, &fileSize) || fileSize.QuadPart <= 0) {
        CloseHandle(h);
        return {};
    }
    // 只读取尾部 4MB，足够覆盖 1603 错误上下文
    const DWORD kMaxRead = 4 * 1024 * 1024;
    DWORD toRead = (fileSize.QuadPart > kMaxRead) ? kMaxRead : (DWORD)fileSize.QuadPart;
    if (fileSize.QuadPart > kMaxRead) {
        LARGE_INTEGER offset = {};
        offset.QuadPart = fileSize.QuadPart - toRead;
        SetFilePointerEx(h, offset, nullptr, FILE_BEGIN);
    }

    std::string data(toRead, '\0');
    DWORD read = 0;
    ReadFile(h, data.data(), toRead, &read, nullptr);
    CloseHandle(h);
    data.resize(read);

    // UTF-16LE → UTF-8；否则按 OEM 代码页解码
    size_t offset = 0;
    if (data.size() >= 2 &&
        static_cast<unsigned char>(data[0]) == 0xFF &&
        static_cast<unsigned char>(data[1]) == 0xFE) {
        offset = 2;
    }
    size_t oddNull = 0;
    for (size_t i = offset + 1; i < data.size(); i += 2) if (data[i] == '\0') oddNull++;
    if (data.size() >= 4 && (offset == 2 || oddNull > (data.size() / 2) / 4)) {
        size_t len = (data.size() - offset) & ~size_t(1);
        int wideLen = static_cast<int>(len / 2);
        int utf8Len = WideCharToMultiByte(CP_UTF8, 0,
            reinterpret_cast<LPCWCH>(data.data() + offset), wideLen,
            nullptr, 0, nullptr, nullptr);
        if (utf8Len > 0) {
            std::string utf8(utf8Len, '\0');
            WideCharToMultiByte(CP_UTF8, 0,
                reinterpret_cast<LPCWCH>(data.data() + offset), wideLen,
                utf8.data(), utf8Len, nullptr, nullptr);
            return utf8;
        }
    }
    std::string utf8 = utils::OemToUtf8(data);
    return utf8.empty() ? data : utf8;
}

bool LooksLikeWslHelp(const std::string& output) {
    std::string lower = ToLowerAscii(output);
    bool hasUsage = lower.find("usage:") != std::string::npos ||
                    lower.find("用法") != std::string::npos ||
                    lower.find("用法:") != std::string::npos;
    bool hasWslFlags = lower.find("--install") != std::string::npos ||
                       lower.find("--distribution") != std::string::npos ||
                       lower.find("-d,") != std::string::npos ||
                       lower.find("--list") != std::string::npos;
    return hasUsage && hasWslFlags;
}

} // namespace

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

// ─── 功能组件是否真正落地 / 是否等待重启 ──────────────────────────────────

bool IsWslComponentInstalled() {
    std::wstring lxssDir = System32Path(L"lxss");
    if (!lxssDir.empty() && PathExists(lxssDir)) return true;

    if (ServiceExists(L"LxssManager")) return true;

    // 服务尚未注册时，注册表项同样能说明组件已落地
    return RegKeyExists(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Services\\LxssManager");
}

bool IsVirtualMachinePlatformComponentInstalled() {
    std::wstring vmcompute = System32Path(L"vmcompute.exe");
    if (!vmcompute.empty() && PathExists(vmcompute)) return true;
    return ServiceExists(L"vmcompute");
}

bool IsRebootPending() {
    if (RegKeyExists(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Component Based Servicing\\RebootPending"))
        return true;

    if (RegKeyExists(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\WindowsUpdate\\Auto Update\\RebootRequired"))
        return true;

    // 不使用 PendingFileRenameOperations：杀毒/驱动安装等也会置位，
    // 容易把“已就绪”误判为“需要重启”。
    return false;
}

// ─── 查找新版 WSL 离线包 ──────────────────────────────────────────────────

std::wstring FindWslPackage(const std::wstring& exeDir) {
    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    const bool isArm64 = (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64);
    const std::wstring arch = isArm64 ? L"arm64" : L"x64";

    std::wstring best;
    int bestScore = -1;

    WIN32_FIND_DATAW ffd;
    std::wstring pattern = exeDir + L"\\wsl*.msi";
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return L"";

    do {
        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;

        std::wstring name = ffd.cFileName;
        std::wstring lower = ToLowerW(name);

        // 旧版 wsl_update_x64.msi 内核 5.10.16，低于 usbipd 要求的 5.10.60.1
        if (lower == L"wsl_update_x64.msi" || lower == L"wsl_update_arm64.msi")
            continue;
        if (lower.size() < 4 || lower.substr(lower.size() - 4) != L".msi")
            continue;

        // 架构匹配优先；明确标记为其它架构的包直接跳过
        int score = 1;
        if (lower.find(arch) != std::wstring::npos) {
            score = 3;
        } else if (lower.find(L"x64") != std::wstring::npos ||
                   lower.find(L"arm64") != std::wstring::npos) {
            continue;
        }
        // 新版命名 wsl.<version>.x64.msi 优先于旧的通用命名
        if (lower.rfind(L"wsl.", 0) == 0) score += 2;

        if (score > bestScore || (score == bestScore && name > best)) {
            bestScore = score;
            best = name;
        }
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);

    if (best.empty()) return L"";
    return exeDir + L"\\" + best;
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

    // 先检查是否已导入：无条件 --import 会返回 ERROR_ALREADY_EXISTS
    auto listed = utils::RunProcess(L"wsl.exe", L"--list --quiet", "", 60000);
    if (listed.exitCode == (DWORD)-1) {
        // 首次调用 VM 冷启动可能超时，重试一次
        listed = utils::RunProcess(L"wsl.exe", L"--list --quiet", "", 60000);
    }
    if (listed.exitCode == 0 &&
        ToLowerAscii(listed.stdOut).find("alpine") != std::string::npos) {
        Logger::Instance().Info("[WslSetup] Alpine already imported, skipping import");
        utils::RunProcess(L"wsl.exe", L"--set-default Alpine", "", 30000);
        return true;
    }

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

    auto r = utils::RunProcess(L"wsl.exe", args, "", 180000);

    if (r.exitCode != 0) {
        std::string err = r.stdOut + " " + r.stdErr;
        std::string lower = ToLowerAscii(err);

        // 导入与启动检查竞争时仍可能出现“已存在”，按成功处理
        if (lower.find("already exists") != std::string::npos ||
            lower.find("error_already_exists") != std::string::npos ||
            lower.find("0x800700b7") != std::string::npos ||
            err.find("已存在") != std::string::npos) {
            Logger::Instance().Warning("[WslSetup] Alpine already exists, treat as success: " +
                                       err);
            utils::RunProcess(L"wsl.exe", L"--set-default Alpine", "", 30000);
            return true;
        }

        if (LooksLikeWslHelp(err)) {
            err = "当前 wsl.exe 为旧版（不支持 --import），请先安装新版 WSL 后重试。";
        }

        Logger::Instance().Error("[WslSetup] wsl --import Alpine failed: " + err);
        if (outError) *outError = err;
        return false;
    }

    // 设置 Alpine 为默认发行版
    utils::RunProcess(L"wsl.exe", L"--set-default Alpine", "", 30000);

    Logger::Instance().Info("[WslSetup] Alpine imported successfully");
    return true;
}

// ─── 安装 MSI ──────────────────────────────────────────────────────────────

bool InstallMsi(const std::wstring& msiPath, std::string* outError,
                bool* outNeedsReboot) {
    if (outNeedsReboot) *outNeedsReboot = false;

    Logger::Instance().Info("[WslSetup] Installing MSI: " +
                            utils::WStringToUtf8(msiPath));

    wchar_t tempDir[MAX_PATH] = {};
    DWORD tempLen = GetTempPathW(MAX_PATH, tempDir);
    std::wstring logPath;
    if (tempLen > 0 && tempLen < MAX_PATH) {
        logPath = std::wstring(tempDir) + L"Ext4WslHelper_msi.log";
        DeleteFileW(logPath.c_str());
    }

    std::wstring args = L"/i \"" + msiPath + L"\" /quiet /norestart";
    if (!logPath.empty()) args += L" /l*v \"" + logPath + L"\"";

    auto r = utils::RunProcess(L"msiexec.exe", args, "", 300000);
    Logger::Instance().Info("[WslSetup] msiexec exit=" + std::to_string(r.exitCode));

    // 0 = 成功；3010 = 成功但需要重启；1638 = 已有更新版本，视为就绪
    if (r.exitCode == 0) return true;
    if (r.exitCode == 3010) {
        if (outNeedsReboot) *outNeedsReboot = true;
        Logger::Instance().Warning("[WslSetup] MSI installed, reboot required (3010)");
        return true;
    }
    if (r.exitCode == 1638) {
        Logger::Instance().Info("[WslSetup] MSI reports newer version already installed (1638)");
        return true;
    }

    // 从 MSI 日志解析 LaunchCondition / 重启语境，给出可操作提示
    std::string log = logPath.empty() ? std::string() : ReadMsiLog(logPath);
    std::string logLower = ToLowerAscii(log);
    bool launchCondition = logLower.find("launchcondition") != std::string::npos ||
                           logLower.find("launch condition") != std::string::npos;
    bool mentionsReboot = logLower.find("reboot") != std::string::npos ||
                          logLower.find("restart") != std::string::npos ||
                          logLower.find("3010") != std::string::npos;
    bool rebootPending = IsRebootPending();

    std::string err;
    if (launchCondition || (r.exitCode == 1603 && (mentionsReboot || rebootPending))) {
        if (outNeedsReboot) *outNeedsReboot = true;
        err = "安装条件未满足，通常是所需的 Windows 功能已启用但组件需要重启后才能生效。"
              "请重启 Windows 后重新运行环境初始化。（MSI exit=" +
              std::to_string(r.exitCode) + "）";
    } else if (r.exitCode == 1618) {
        err = "另一个安装程序正在运行，请等待其完成后重试。";
    } else {
        err = "MSI 安装失败（exit=" + std::to_string(r.exitCode) + "）";
        if (!r.stdErr.empty()) err += ": " + r.stdErr;
        else if (!r.stdOut.empty()) err += ": " + r.stdOut;
    }

    Logger::Instance().Error("[WslSetup] " + err);
    if (!log.empty()) {
        Logger::Instance().Debug("[WslSetup] MSI log tail: " +
            log.substr(log.size() > 1500 ? log.size() - 1500 : 0));
    }
    if (outError) *outError = err;
    return false;
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
