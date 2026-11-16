#include "WslManager.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include <sstream>
#include <algorithm>
#include <array>

WslManager::WslManager(ICommandExecutor* executor)
    : executor_(executor) {
}

// 直接通过 CreateProcessW 执行 wsl.exe --list --verbose，绕过 PowerShell 的编码/控制台问题
static std::string RunWslListDirect() {
#ifdef _WIN32
    std::string fullCmd = "wsl.exe --list --verbose";

    HANDLE hStdInRd = nullptr, hStdInWr = nullptr;
    HANDLE hStdOutRd = nullptr, hStdOutWr = nullptr;
    HANDLE hStdErrRd = nullptr, hStdErrWr = nullptr;
    SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };

    // 始终创建 stdin 管道，避免 GUI 程序无控制台时 GetStdHandle 返回空句柄
    if (!CreatePipe(&hStdOutRd, &hStdOutWr, &sa, 0)) return {};
    if (!CreatePipe(&hStdErrRd, &hStdErrWr, &sa, 0)) {
        CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
        return {};
    }
    if (!CreatePipe(&hStdInRd, &hStdInWr, &sa, 0)) {
        CloseHandle(hStdOutRd); CloseHandle(hStdOutWr);
        CloseHandle(hStdErrRd); CloseHandle(hStdErrWr);
        return {};
    }
    SetHandleInformation(hStdOutRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdErrRd, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hStdInWr, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hStdOutWr;
    si.hStdError = hStdErrWr;
    si.hStdInput = hStdInRd;

    // 关闭写端，子进程立即读到 EOF
    CloseHandle(hStdInWr);
    hStdInWr = nullptr;

    std::wstring wCmd = utils::Utf8ToWString(fullCmd);
    PROCESS_INFORMATION pi = {};

    BOOL created = CreateProcessW(nullptr, wCmd.data(), nullptr, nullptr, TRUE,
                                  CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);

    CloseHandle(hStdOutWr);
    CloseHandle(hStdErrWr);
    CloseHandle(hStdInRd);

    if (!created) {
        CloseHandle(hStdOutRd);
        CloseHandle(hStdErrRd);
        return {};
    }

    // 首次调用可能触发 WSL VM 冷启动，实测可超过 10 秒
    DWORD waitResult = WaitForSingleObject(pi.hProcess, 60000);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        CloseHandle(hStdOutRd);
        CloseHandle(hStdErrRd);
        return {};
    }

    // 读取 stdout
    std::string output;
    std::array<char, 4096> buf;
    DWORD bytesRead = 0;
    while (ReadFile(hStdOutRd, buf.data(), (DWORD)buf.size() - 1, &bytesRead, nullptr) &&
           bytesRead > 0) {
        output.append(buf.data(), bytesRead);
    }

    // 如果 stdout 为空，尝试读取 stderr
    if (output.empty()) {
        while (ReadFile(hStdErrRd, buf.data(), (DWORD)buf.size() - 1, &bytesRead, nullptr) &&
               bytesRead > 0) {
            output.append(buf.data(), bytesRead);
        }
    }

    CloseHandle(hStdOutRd);
    CloseHandle(hStdErrRd);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    // wsl.exe 在管道模式下输出 OEM 代码页文本（如中文 GBK），转为 UTF-8
    if (!output.empty()) {
        std::string utf8 = utils::OemToUtf8(output);
        if (!utf8.empty()) return utf8;
    }
    return output;
#else
    return {};
#endif
}

std::vector<WslDistribution> WslManager::ListDistributions() {
    std::vector<WslDistribution> distros;

    // 主路径：通过 executor（外部命令检测 → RunProcess → OEM→UTF-8，且会转发到 debug 窗口）
    // 首次调用（VM 冷启动）可能超过 10s，给 60s 并在超时后重试一次
    std::string output;
    auto result = executor_->Execute("wsl --list --verbose", "", 60000);
    if (result.exitCode == (DWORD)-1) {
        result = executor_->Execute("wsl --list --verbose", "", 60000);
    }
    if (result.exitCode == 0 && !result.stdOut.empty()) {
        output = result.stdOut;
    } else {
        // 回退：直接 CreateProcess（单元测试 mock 不可用时）
        output = RunWslListDirect();
    }

    // Parse output format (fixed-width columns):
    //   NAME                   STATE           VERSION
    // * Ubuntu                 Running         2
    //   Debian                 Stopped         1
    //
    // Asterisk in first column = default distro.
    // We parse column positions from the English header line,
    // so state text can be in any language.

    std::istringstream stream(output);
    std::string line;
    size_t stateCol = std::string::npos;
    size_t versionCol = std::string::npos;

    while (std::getline(stream, line)) {
        // Determine column positions from the header line
        if (stateCol == std::string::npos &&
            line.find("NAME") != std::string::npos &&
            line.find("VERSION") != std::string::npos) {
            stateCol = line.find("STATE");
            versionCol = line.find("VERSION");
            continue;
        }

        std::string trimmed = utils::Trim(line);
        if (trimmed.empty()) continue;

        WslDistribution distro{};
        distro.isDefault = false;
        distro.isRunning = false;

        // Check for default marker
        size_t nameStart = 0;
        if (!line.empty() && line[0] == '*') {
            distro.isDefault = true;
            nameStart = 1;
        }

        // Extract fields by column position (language-independent)
        if (stateCol != std::string::npos && versionCol != std::string::npos) {
            // NAME = from nameStart to state column
            distro.name = utils::Trim(line.substr(nameStart, stateCol - nameStart));

            // STATE text (not used for critical logic, but useful for isRunning)
            std::string stateText = utils::Trim(line.substr(stateCol, versionCol - stateCol));
            distro.isRunning = (stateText.find("Running") != std::string::npos);

            // VERSION = last token after versionCol
            std::string versionArea = utils::Trim(line.substr(versionCol));
            // Find the first digit token (1 or 2) from the left
            for (auto& c : versionArea) {
                if (c == '1') { distro.version = "1"; break; }
                if (c == '2') { distro.version = "2"; break; }
            }
        }

        if (!distro.name.empty() && !distro.version.empty()) {
            distros.push_back(distro);
        }
    }

    return distros;
}

WslDistribution WslManager::GetDefaultDistribution() {
    auto distros = ListDistributions();
    for (const auto& d : distros) {
        if (d.isDefault) return d;
    }
    // Return first available if no default marked
    if (!distros.empty()) return distros[0];
    return {};
}

bool WslManager::IsExt4Supported(const std::string& distro) {
    // Only WSL2 supports usbipd passthrough
    auto distros = ListDistributions();
    for (const auto& d : distros) {
        if (d.name == distro) {
            return d.IsWsl2();
        }
    }
    return false;
}

std::string WslManager::DetectBlockDevice(const std::string& previousDisks) {
    std::string wslPrefix = distro_.empty() ? "" : "wsl -d " + distro_ + " -- ";

    // 用 cat /proc/partitions 获取完整分区表，与 attach 前快照比对
    // 输出格式: major minor #blocks name
    //              8    0  62512344 sda
    //              8    1  62512344 sda1
    auto partsResult = executor_->Execute(wslPrefix + "cat /proc/partitions", "", 30000);
    if (partsResult.exitCode != 0) {
        Logger::Instance().Error("[WslManager] cat /proc/partitions failed");
        return "";
    }

    std::string newDevice;
    std::istringstream currentStream(partsResult.stdOut);
    std::string line;
    while (std::getline(currentStream, line)) {
        std::string trimmed = utils::Trim(line);
        if (trimmed.empty()) continue;

        std::istringstream lineStream(trimmed);
        int major = 0, minor = 0;
        long long blocks = 0;
        std::string name;
        lineStream >> major >> minor >> blocks >> name;
        if (name.empty()) continue;

        // 只关心 SCSI 磁盘设备 (sdX)，跳过分区 (sdX1)
        if (name.size() < 3 || name[0] != 's' || name[1] != 'd') continue;
        if (name.size() > 3 && isdigit((unsigned char)name[3])) continue;
        if (name.find("loop") != std::string::npos) continue;

        // 与 attach 前的快照比对，找新设备
        if (previousDisks.find(name) == std::string::npos) {
            newDevice = name;
            Logger::Instance().Debug("[WslManager] New device from /proc/partitions: " + newDevice);
            break;
        }
    }

    return newDevice;
}

bool WslManager::EnsureUsbStorageModule() {
    std::string wslPrefix = distro_.empty() ? "" : "wsl -d " + distro_ + " -- ";

    // 先确保 WSL 内可执行命令（先跑一个简单命令热身，避免 WSL 启动竞争）
    executor_->Execute(wslPrefix + "true", "", 60000);

    // 第一步：检查 usb_storage 是否已加载
    auto check = executor_->Execute(wslPrefix + "lsmod | grep usb_storage", "", 30000);
    if (check.exitCode == 0 && !check.stdOut.empty()) {
        Logger::Instance().Info("[WslManager] usb_storage module already loaded");
        return true;
    }

    Logger::Instance().Info("[WslManager] usb_storage not loaded, loading with modprobe...");

    // 第二步：尝试 modprobe，捕获完整输出
    auto mod = executor_->Execute(wslPrefix + "modprobe usb-storage 2>&1", "", 30000);
    Logger::Instance().Info("[WslManager] modprobe exit=" + std::to_string(mod.exitCode) +
                            " out=[" + mod.stdOut + "] err=[" + mod.stdErr + "]");

    if (mod.exitCode != 0) {
        // modprobe 失败，尝试 depmod -a 重建依赖再试
        Logger::Instance().Info("[WslManager] modprobe failed, trying depmod...");
        executor_->Execute(wslPrefix + "depmod -a 2>&1", "", 60000);
        auto mod2 = executor_->Execute(wslPrefix + "modprobe usb-storage 2>&1", "", 30000);
        Logger::Instance().Info("[WslManager] modprobe (after depmod) exit=" +
                                std::to_string(mod2.exitCode) +
                                " out=[" + mod2.stdOut + "] err=[" + mod2.stdErr + "]");
        if (mod2.exitCode != 0) {
            Logger::Instance().Warning("[WslManager] usb_storage still not found after modprobe+depmod");
            return false;
        }
    }

    Sleep(500); // 等待模块初始化

    // 第三步：lsmod 确认
    auto verify = executor_->Execute(wslPrefix + "lsmod | grep usb_storage", "", 30000);
    bool nowLoaded = (verify.exitCode == 0 && !verify.stdOut.empty());
    if (nowLoaded) {
        Logger::Instance().Info("[WslManager] usb_storage loaded (confirmed via lsmod)");
        return true;
    }

    Logger::Instance().Warning("[WslManager] lsmod confirmation failed after modprobe succeeded");
    return true; // modprobe 成功了但 lsmod 没看到，可能是 busybox lsmod 差异，视为成功
}

std::string WslManager::GetDiskList() {
    std::string wslPrefix = distro_.empty() ? "" : "wsl -d " + distro_ + " -- ";
    auto result = executor_->Execute(wslPrefix + "cat /proc/partitions", "", 30000);
    if (result.exitCode == 0) {
        return result.stdOut;
    }
    return "";
}

std::string WslManager::MountExt4(const std::string& device,
                                   const std::string& mountPoint,
                                   const std::string& sudoPassword,
                                   bool& isAuthError) {
    std::string wslPrefix = distro_.empty() ? "" : "wsl -d " + distro_ + " -- ";
    isAuthError = false;

    // Alpine WSL 默认 root 用户，空密码 = 无需 sudo
    bool needSudo = !sudoPassword.empty();

    // 如需 sudo，先验证密码
    if (needSudo) {
        std::string testCmd = wslPrefix + "sudo -S echo ok";
        auto testResult = executor_->Execute(testCmd, sudoPassword + "\n", 30000);
        if (testResult.exitCode != 0) {
            std::string err = testResult.stdOut + " " + testResult.stdErr;
            if (err.find("incorrect password") != std::string::npos ||
                err.find("try again") != std::string::npos ||
                err.find("Sorry") != std::string::npos ||
                err.find("密码") != std::string::npos) {
                isAuthError = true;
                return "sudo 密码错误";
            }
            return "sudo 验证失败: " + err;
        }
    }

    // Create mount point
    std::string mkdirCmd = needSudo
        ? wslPrefix + "sudo -S mkdir -p " + mountPoint
        : wslPrefix + "mkdir -p " + mountPoint;
    auto mkdirResult = executor_->Execute(mkdirCmd, needSudo ? sudoPassword + "\n" : "", 30000);
    if (mkdirResult.exitCode != 0) {
        std::string errMsg = "创建挂载点失败: " + mkdirResult.stdErr;
        Logger::Instance().Error("[WslManager] " + errMsg);
        return errMsg;
    }

    // Mount — 先尝试读写，失败则只读
    auto mountWithFlags = [&](const std::string& flags) -> std::string {
        std::string cmd;
        if (needSudo) {
            cmd = flags.empty()
                ? wslPrefix + "sudo -S mount -t ext4 /dev/" + device + " " + mountPoint
                : wslPrefix + "sudo -S mount -t ext4 " + flags + " /dev/" + device + " " + mountPoint;
        } else {
            cmd = flags.empty()
                ? wslPrefix + "mount -t ext4 /dev/" + device + " " + mountPoint
                : wslPrefix + "mount -t ext4 " + flags + " /dev/" + device + " " + mountPoint;
        }
        auto r = executor_->Execute(cmd, needSudo ? sudoPassword + "\n" : "", 30000);
        return r.exitCode == 0 ? "" : r.stdOut + " " + r.stdErr;
    };

    std::string mountErr = mountWithFlags("");
    if (!mountErr.empty()) {
        // 以只读方式重试
        mountErr = mountWithFlags("-o ro");
        if (!mountErr.empty()) {
            Logger::Instance().Error("[WslManager] Mount failed (rw+ro): " + mountErr);
            return "挂载失败: " + mountErr;
        }
        Logger::Instance().Warning("[WslManager] Mounted read-only for " + device);
    }

    Logger::Instance().Info("[WslManager] Mounted /dev/" + device +
                            " -> " + mountPoint);
    return "";
}

bool WslManager::IsMounted(const std::string& mountPoint) {
    std::string wslPrefix = distro_.empty() ? "" : "wsl -d " + distro_ + " -- ";
    auto r = executor_->Execute(wslPrefix + "mountpoint -q " + mountPoint, "", 15000);
    return (r.exitCode == 0);
}

bool WslManager::Unmount(const std::string& mountPoint,
                          const std::string& sudoPassword) {
    std::string wslPrefix = distro_.empty() ? "" : "wsl -d " + distro_ + " -- ";
    bool needSudo = !sudoPassword.empty();

    std::string cmd = needSudo
        ? wslPrefix + "sudo -S umount " + mountPoint
        : wslPrefix + "umount " + mountPoint;

    // 循环卸载，每次去掉一层挂载（支持多次挂载场景）
    for (int i = 0; i < 10; i++) {
        auto result = executor_->Execute(cmd, needSudo ? sudoPassword + "\n" : "", 30000);

        if (result.exitCode == 0) {
            Logger::Instance().Info("[WslManager] Unmounted one layer at " + mountPoint);
            Sleep(200);
            continue;
        }

        // 未挂载或已卸完 → 成功
        if (result.stdOut.find("not mounted") != std::string::npos ||
            result.stdErr.find("not mounted") != std::string::npos) {
            Logger::Instance().Info("[WslManager] " + mountPoint + " fully unmounted");
            return true;
        }

        // 真实错误（如 device busy）→ 尝试 lazy umount
        Logger::Instance().Warning("[WslManager] umount failed, trying lazy: " + result.stdErr);
        std::string lazyCmd = needSudo
            ? wslPrefix + "sudo -S umount -l " + mountPoint
            : wslPrefix + "umount -l " + mountPoint;
        auto lazyResult = executor_->Execute(lazyCmd, needSudo ? sudoPassword + "\n" : "", 30000);
        if (lazyResult.exitCode != 0) {
            Logger::Instance().Error("[WslManager] Unmount failed: " +
                                     result.stdOut + " " + result.stdErr);
            return false;
        }
        Logger::Instance().Info("[WslManager] Lazy unmount done for " + mountPoint);
        return true;
    }

    Logger::Instance().Error("[WslManager] Unmount failed after max retries");
    return false;
}

std::string WslManager::MakeWindowsPath(const std::string& distro,
                                         const std::string& mountPoint) {
    // \\wsl.localhost\<distro>\<mountpoint>
    // e.g. \\wsl.localhost\Ubuntu\mnt\ext4-sd-card
    std::string path = "\\\\wsl.localhost\\" + distro;
    std::string mountCopy = mountPoint;
    // Replace forward slashes with backslashes
    std::replace(mountCopy.begin(), mountCopy.end(), '/', '\\');
    path += mountCopy;
    return path;
}
