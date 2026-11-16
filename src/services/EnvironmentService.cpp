#include "EnvironmentService.h"
#include "../common/WslSetup.h"
#include "../common/Utils.h"
#include "../common/Logger.h"
#include <algorithm>
#include <cctype>

EnvironmentService::EnvironmentService(ICommandExecutor* executor)
    : executor_(executor) {}

EnvironmentStatus EnvironmentService::CheckAll() {
    EnvironmentStatus status;
    status.virt        = CheckVirtualization();
    status.wslFeature  = CheckWslFeature();
    status.wsl         = CheckWsl();
    status.alpine      = CheckAlpine();
    status.usbipd      = CheckUsbipd();
    return status;
}

// ── 虚拟化使能检测 ────────────────────────────────────────────────────────
//  通过 dism 查询 VirtualMachinePlatform 功能状态判断。
//  VMP 需要硬件虚拟化支持且在固件中启用，若 VMP 已启用则虚拟化必然就绪。

VirtCheckResult EnvironmentService::CheckVirtualization() {
    VirtCheckResult result;

    auto r = executor_->Execute(
        "dism /online /get-featureinfo /featurename:VirtualMachinePlatform", "", 30000);

    if (r.exitCode != 0) {
        result.state = CheckState::NotInstalled;
        result.detail = "虚拟化检测失败";
        return result;
    }

    DismFeatureState featureState = ParseDismFeatureState(r.stdOut);
    bool enabled = (featureState == DismFeatureState::Enabled ||
                    featureState == DismFeatureState::EnablePending);
    result.isEnabled = enabled;

    // dism 明确报告 Enable Pending：功能已写入但尚未生效，必须重启
    if (featureState == DismFeatureState::EnablePending) {
        result.state = CheckState::NotInstalled;
        result.needsReboot = true;
        result.detail = "虚拟化功能已启用但仍在等待重启 — 请重启 Windows 后重试";
        Logger::Instance().Warning("[EnvCheck] VirtualMachinePlatform state=EnablePending");
        return result;
    }
    if (featureState != DismFeatureState::Enabled) {
        result.state = CheckState::NotInstalled;
        result.detail = "虚拟化未启用";
        return result;
    }

    // dism 显示已启用时，再确认组件是否真正落地。
    // 注意：全局 RebootPending 可能由无关更新置位，因此只在组件缺失时提示重启，
    // 组件已存在时不应因为其它更新而把可用的环境判为未就绪。
    bool componentInstalled = wslsetup::IsVirtualMachinePlatformComponentInstalled();
    bool rebootPending = wslsetup::IsRebootPending();
    if (!componentInstalled) {
        result.state = CheckState::NotInstalled;
        result.needsReboot = true;
        result.detail = "虚拟化功能已启用，但组件尚未生效 — 请重启 Windows 后重试";
    } else {
        result.state = CheckState::Ready;
        result.detail = "虚拟化已启用";
    }

    Logger::Instance().Info("[EnvCheck] VirtualMachinePlatform enabled=" +
                            std::to_string(enabled) +
                            " component=" + std::to_string(componentInstalled) +
                            " rebootPending=" + std::to_string(rebootPending) +
                            " needsReboot=" + std::to_string(result.needsReboot));
    return result;
}

// ── WSL 功能使能检测 ───────────────────────────────────────────────────────
//  通过 dism 查询 Microsoft-Windows-Subsystem-Linux 功能状态

WslFeatureCheckResult EnvironmentService::CheckWslFeature() {
    WslFeatureCheckResult result;

    auto r = executor_->Execute(
        "dism /online /get-featureinfo /featurename:Microsoft-Windows-Subsystem-Linux", "", 30000);

    if (r.exitCode != 0) {
        result.state = CheckState::NotInstalled;
        result.detail = "WSL 功能检测失败";
        return result;
    }

    DismFeatureState featureState = ParseDismFeatureState(r.stdOut);
    bool enabled = (featureState == DismFeatureState::Enabled ||
                    featureState == DismFeatureState::EnablePending);
    result.isEnabled = enabled;

    // dism 明确报告 Enable Pending：功能已写入但尚未生效，必须重启
    if (featureState == DismFeatureState::EnablePending) {
        result.state = CheckState::NotInstalled;
        result.needsReboot = true;
        result.detail = "WSL 功能已启用但仍在等待重启 — 请重启 Windows 后重试";
        Logger::Instance().Warning("[EnvCheck] WSL feature state=EnablePending");
        return result;
    }
    if (featureState != DismFeatureState::Enabled) {
        result.state = CheckState::NotInstalled;
        result.detail = "WSL 功能未启用";
        return result;
    }

    // dism 显示“已启用”只是标志位，必须确认 lxss 组件已落地。
    // 全局 RebootPending 只在组件缺失时作为“需要重启”的佐证。
    bool componentInstalled = wslsetup::IsWslComponentInstalled();
    bool rebootPending = wslsetup::IsRebootPending();
    if (!componentInstalled) {
        result.state = CheckState::NotInstalled;
        result.needsReboot = true;
        result.detail = "WSL 功能已启用，但组件尚未生效 — 请重启 Windows 后重试";
    } else {
        result.state = CheckState::Ready;
        result.detail = "WSL 功能已启用";
    }

    Logger::Instance().Info("[EnvCheck] WSL feature enabled=" +
                            std::to_string(enabled) +
                            " component=" + std::to_string(componentInstalled) +
                            " rebootPending=" + std::to_string(rebootPending) +
                            " needsReboot=" + std::to_string(result.needsReboot));
    return result;
}

// ── WSL 内核检测 ──────────────────────────────────────────────────────────
//  wsl.exe --version 输出示例：
//    WSL version: 2.0.9.0
//    Kernel version: 5.15.133.1-1
//
//  除了“有输出”之外，还必须校验：
//    1) WSL 版本 >= 2.x
//    2) 内核版本 >= 5.10.60.1（usbipd 的最低要求）
//  旧版 wsl.exe 不认识 --version，会打印帮助页并以 exit=1 退出。

WslCheckResult EnvironmentService::CheckWsl() {
    WslCheckResult result;

    auto r = RunWsl("wsl.exe --version");
    std::string combined = r.stdOut + "\n" + r.stdErr;

    if (result.version.empty() && result.kernelVersion.empty() &&
        LooksLikeWslHelpOutput(combined)) {
        result.state = CheckState::VersionMismatch;
        result.detail = "当前 wsl.exe 为旧版（不支持 --version），请安装新版 WSL";
        Logger::Instance().Warning("[EnvCheck] " + result.detail);
        return result;
    }

    if (r.exitCode == 0 && !r.stdOut.empty()) {
        auto lines = utils::Split(r.stdOut, '\n');
        for (auto& line : lines) {
            line = utils::Trim(line);
            auto colon = line.find_first_of(":：");
            if (colon == std::string::npos) continue;
            std::string value = utils::Trim(line.substr(colon + 1));
            if (value.empty()) continue;

            if (line.find("WSL version") != std::string::npos ||
                line.find("WSL 版本") != std::string::npos) {
                result.version = value;
            } else if (line.find("Kernel version") != std::string::npos ||
                       line.find("内核版本") != std::string::npos) {
                result.kernelVersion = value;
            }
        }
    }

    if (!result.version.empty() || !result.kernelVersion.empty()) {
        bool wsl2 = IsWslVersionSupported(result.version);
        bool kernelOk = IsKernelVersionSupported(result.kernelVersion);

        if (!wsl2) {
            result.state = CheckState::VersionMismatch;
            result.detail = "WSL " + result.version + " — 需要 WSL2";
        } else if (!kernelOk) {
            result.state = CheckState::VersionMismatch;
            result.detail = "WSL " + result.version + " 内核 " + result.kernelVersion +
                            " 低于 " + wslsetup::WSL_MIN_KERNEL_VERSION +
                            "，请安装新版 WSL";
        } else {
            result.state = CheckState::Ready;
            result.detail = "WSL2 " + result.version + " (内核 " + result.kernelVersion + ")";
        }
        Logger::Instance().Info("[EnvCheck] " + result.detail);
        return result;
    }

    // 回退：旧版本 wsl 不支持 --version
    auto r2 = RunWsl("wsl.exe --list --verbose");
    if (r2.exitCode == 0 && !r2.stdOut.empty() &&
        !LooksLikeWslHelpOutput(r2.stdOut + "\n" + r2.stdErr)) {
        result.state = CheckState::VersionMismatch;
        result.version = "1.x";
        result.detail = "旧版 WSL（不支持 --version）— 请安装新版 WSL2";
    } else {
        result.state = CheckState::NotInstalled;
        if (wslsetup::IsWslComponentInstalled() && wslsetup::IsRebootPending()) {
            result.detail = "WSL 功能已启用，但组件需要重启 — 请重启 Windows";
        } else {
            result.detail = "WSL 未安装或未就绪";
        }
    }

    Logger::Instance().Info("[EnvCheck] " + result.detail);
    return result;
}

// ── 版本 / 输出解析（可单元测试） ────────────────────────────────────────

DismFeatureState EnvironmentService::ParseDismFeatureState(const std::string& dismOutput) {
    std::string lower = dismOutput;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    auto has = [&lower](const char* text) {
        return lower.find(text) != std::string::npos;
    };

    // 顺序敏感：先判断 Pending，再判断 Enabled/Disabled
    if (has("enable pending") || has("启用挂起") || has("啟用擱置"))
        return DismFeatureState::EnablePending;
    if (has("disable pending") || has("禁用挂起") || has("停用擱置"))
        return DismFeatureState::DisablePending;
    if (has("enabled") || has("已启用") || has("已啟用"))
        return DismFeatureState::Enabled;
    if (has("disabled") || has("已禁用"))
        return DismFeatureState::Disabled;

    return DismFeatureState::Unknown;
}

bool EnvironmentService::IsWslVersionSupported(const std::string& version) {
    if (version.empty()) return false;
    return utils::CompareVersion(version, wslsetup::WSL_MIN_VERSION) >= 0;
}

bool EnvironmentService::IsKernelVersionSupported(const std::string& kernelVersion) {
    if (kernelVersion.empty()) return false;
    return utils::CompareVersion(kernelVersion, wslsetup::WSL_MIN_KERNEL_VERSION) >= 0;
}

bool EnvironmentService::LooksLikeWslHelpOutput(const std::string& output) {
    std::string lower = output;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });

    bool hasUsage = lower.find("usage:") != std::string::npos ||
                    lower.find("用法") != std::string::npos;
    bool hasWslFlags = lower.find("--install") != std::string::npos ||
                       lower.find("--distribution") != std::string::npos ||
                       lower.find("--list") != std::string::npos ||
                       lower.find("-d,") != std::string::npos;
    return hasUsage && hasWslFlags;
}

// ── WSL 命令执行：首次 60s，后续 15s，超时重试一次 ────────────────────────

CommandResult EnvironmentService::RunWsl(const std::string& commandLine) {
    const DWORD firstTimeoutMs = 60000;
    const DWORD followupTimeoutMs = 15000;

    auto r = executor_->Execute(commandLine, "",
                                wslWarmedUp_ ? followupTimeoutMs : firstTimeoutMs);
    if (!wslWarmedUp_ && r.exitCode == (DWORD)-1) {
        Logger::Instance().Warning(
            "[EnvCheck] WSL first call timed out, retrying with 60s: " + commandLine);
        r = executor_->Execute(commandLine, "", firstTimeoutMs);
    }
    wslWarmedUp_ = true;
    return r;
}

// ── Alpine 发行版检测 ─────────────────────────────────────────────────────
//  wsl.exe --list --verbose 中查找 Alpine

AlpineCheckResult EnvironmentService::CheckAlpine() {
    AlpineCheckResult result;

    auto r = RunWsl("wsl.exe --list --verbose");

    if (r.exitCode == 0) {
        auto lines = utils::Split(r.stdOut, '\n');
        for (auto& line : lines) {
            line = utils::Trim(line);
            if (line.find("NAME") != std::string::npos) continue; // 标题行
            if (line.find("Alpine") == std::string::npos) continue;

            result.state = CheckState::Ready;
            auto parts = utils::Split(line, ' ');
            std::string stateLabel = "已停止";
            for (size_t i = 0; i < parts.size(); i++) {
                if (parts[i] == "Alpine" && i + 1 < parts.size()) {
                    if (parts[i + 1] == "Running") stateLabel = "运行中";
                    break;
                }
            }
            result.detail = "Alpine (" + stateLabel + ")";
            break;
        }

        if (result.state != CheckState::Ready) {
            result.state = CheckState::NotInstalled;
            result.detail = "Alpine 未安装";
        }
    } else {
        result.state = CheckState::NotInstalled;
        result.detail = "Alpine 未安装";
    }

    return result;
}

// ── usbipd 检测 ───────────────────────────────────────────────────────────

UsbipdCheckResult EnvironmentService::CheckUsbipd() {
    UsbipdCheckResult result;

    auto r = executor_->Execute("usbipd.exe", "", 10000);

    if (r.exitCode != (DWORD)-1) {
        result.state = CheckState::Ready;
        result.detail = "usbipd-win 已安装";
    } else {
        result.state = CheckState::NotInstalled;
        result.detail = "usbipd-win 未安装";
    }

    return result;
}
