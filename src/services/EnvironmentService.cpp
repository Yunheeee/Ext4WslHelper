#include "EnvironmentService.h"
#include "../common/Utils.h"
#include "../common/Logger.h"

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

    if (r.exitCode == 0) {
        bool enabled = (r.stdOut.find("Enabled") != std::string::npos ||
                        r.stdOut.find("\xe5\xb7\xb2\xe5\x90\xaf\xe7\x94\xa8") != std::string::npos);
        result.isEnabled = enabled;
        result.state = enabled ? CheckState::Ready : CheckState::NotInstalled;
        result.detail = enabled ? "虚拟化已启用" : "虚拟化未启用";
    } else {
        result.state = CheckState::NotInstalled;
        result.detail = "虚拟化检测失败";
    }

    return result;
}

// ── WSL 功能使能检测 ───────────────────────────────────────────────────────
//  通过 dism 查询 Microsoft-Windows-Subsystem-Linux 功能状态

WslFeatureCheckResult EnvironmentService::CheckWslFeature() {
    WslFeatureCheckResult result;

    auto r = executor_->Execute(
        "dism /online /get-featureinfo /featurename:Microsoft-Windows-Subsystem-Linux", "", 30000);

    if (r.exitCode == 0) {
        bool enabled = (r.stdOut.find("Enabled") != std::string::npos ||
                        r.stdOut.find("\xe5\xb7\xb2\xe5\x90\xaf\xe7\x94\xa8") != std::string::npos);
        result.isEnabled = enabled;
        result.state = enabled ? CheckState::Ready : CheckState::NotInstalled;
        result.detail = enabled ? "WSL 功能已启用" : "WSL 功能未启用";
    } else {
        result.state = CheckState::NotInstalled;
        result.detail = "WSL 功能检测失败";
    }

    return result;
}

// ── WSL 内核检测 ──────────────────────────────────────────────────────────
//  wsl.exe --version 输出示例：
//    WSL version: 2.0.9.0
//    Kernel version: 5.15.133.1-1
//
//  回退：wsl.exe --list --verbose（旧版本 wsl 不支持 --version）

WslCheckResult EnvironmentService::CheckWsl() {
    WslCheckResult result;

    auto r = executor_->Execute("wsl.exe --version", "", 10000);

    if (r.exitCode == 0 && !r.stdOut.empty()) {
        auto lines = utils::Split(r.stdOut, '\n');
        for (auto& line : lines) {
            line = utils::Trim(line);
            if (line.find("WSL version:") != std::string::npos) {
                auto parts = utils::Split(line, ' ');
                if (parts.size() >= 3) {
                    result.version = parts[2];
                    result.state = (!result.version.empty() && result.version[0] == '2')
                                       ? CheckState::Ready
                                       : CheckState::VersionMismatch;
                }
            }
            if (line.find("Kernel version:") != std::string::npos) {
                auto parts = utils::Split(line, ' ');
                if (parts.size() >= 3) {
                    result.kernelVersion = parts[2];
                }
            }
        }

        if (result.state == CheckState::Ready) {
            result.detail = "WSL2 " + result.version;
            if (!result.kernelVersion.empty())
                result.detail += " (内核 " + result.kernelVersion + ")";
        } else if (result.state == CheckState::VersionMismatch) {
            result.detail = "WSL " + result.version + " — 需要 WSL2";
        } else {
            result.state = CheckState::Ready;
            result.detail = "WSL 已安装";
        }
    } else {
        auto r2 = executor_->Execute("wsl.exe --list --verbose", "", 10000);
        if (r2.exitCode == 0) {
            result.state = CheckState::VersionMismatch;
            result.version = "1.x";
            result.detail = "WSL1 — 请升级到 WSL2";
        } else {
            result.state = CheckState::NotInstalled;
            result.detail = "WSL 未安装";
        }
    }

    return result;
}

// ── Alpine 发行版检测 ─────────────────────────────────────────────────────
//  wsl.exe --list --verbose 中查找 Alpine

AlpineCheckResult EnvironmentService::CheckAlpine() {
    AlpineCheckResult result;

    auto r = executor_->Execute("wsl.exe --list --verbose", "", 10000);

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
