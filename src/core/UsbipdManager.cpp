#include "UsbipdManager.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include <sstream>

// ─── 错误关键字表 ────────────────────────────────────────────────────────────
// 人为可审阅、可追加：在此添加/修改错误匹配规则即可影响所有错误分类行为。
// 顺序敏感：靠前的条目优先匹配。
// clang-format off
const UsbipdErrorEntry kUsbipdErrorTable[] = {
    { "not found",             "usbipd.exe 未找到",              "请安装 usbipd-win (winget install usbipd)" },
    { "not recognized",        "设备未被系统识别",               "检查设备驱动或重新插拔" },
    { "not attached",          "设备未附加到 WSL",               "已是预期状态（操作为幂等）" },
    { "device busy",           "设备被占用",                     "重启 usbipd 服务后重试" },
    { "already exported",      "设备已导出到其他 WSL",           "先执行 detach 再重试" },
    { "already attached",      "设备已附加到 WSL",               "已是预期状态" },
    { "access denied",         "权限不足",                       "请以管理员身份运行本程序" },
    { "permission denied",     "权限不足",                       "请以管理员身份运行本程序" },
    { "timeout",               "操作超时",                       "检查网络连接或 WSL 状态" },
    { "service not",           "usbipd 服务未运行",              "启动 usbipd 服务 (sc start usbipd)" },
    { "not supported",         "不支持的操作",                   "请更新 usbipd-win 到最新版本" },
    { "failed to",             "操作执行失败",                   "查看详细错误信息后重试" },
    { "error",                 "未知错误",                       "查看详细错误信息后重试" },
};
// clang-format on
const int kUsbipdErrorTableSize = sizeof(kUsbipdErrorTable) / sizeof(kUsbipdErrorTable[0]);

UsbipdManager::UsbipdManager(ICommandExecutor* executor)
    : executor_(executor) {
}

UsbipdErrorInfo UsbipdManager::ClassifyError(const CommandResult& result) {
    UsbipdErrorInfo info;
    info.rawOutput = result.stdOut + " " + result.stdErr;

    if (result.exitCode == 0) {
        info.errorType = "成功";
        return info;
    }

    // 按下标顺序匹配，返回第一个命中的条目
    for (int i = 0; i < kUsbipdErrorTableSize; i++) {
        if (info.rawOutput.find(kUsbipdErrorTable[i].keyword) != std::string::npos) {
            info.errorType = kUsbipdErrorTable[i].errorType;
            info.matchedKeyword = kUsbipdErrorTable[i].keyword;
            info.suggestion = kUsbipdErrorTable[i].suggestion;
            return info;
        }
    }

    // 无匹配：兜底
    info.errorType = "未分类错误";
    info.suggestion = "查看详细错误信息后重试或反馈给开发者";
    return info;
}

std::vector<UsbipdDeviceInfo> UsbipdManager::List() {
    std::vector<UsbipdDeviceInfo> devices;

    auto result = executor_->Execute("usbipd list", "", 15000);
    if (result.exitCode != 0) {
        lastError_ = ClassifyError(result);
        Logger::Instance().Error("[UsbipdManager] usbipd list failed: " +
                                 result.stdOut + " | " + result.stdErr +
                                 " -> " + lastError_.errorType);

        if (lastError_.matchedKeyword == "not found")
            state_ = UsbipdState::NotInstalled;
        else
            state_ = UsbipdState::Error;
        return devices;
    }

    state_ = UsbipdState::ServiceRunning;

    // Parse output: skip header, parse data lines
    std::istringstream stream(result.stdOut);
    std::string line;
    bool inDataSection = false;

    while (std::getline(stream, line)) {
        std::string trimmed = utils::Trim(line);

        // Skip empty lines and section headers
        if (trimmed.empty() || trimmed == "Connected:" || trimmed == "Persisted:") {
            inDataSection = false;
            continue;
        }

        // Skip header line (contains BUSID, VID:PID, DEVICE, STATE)
        if (trimmed.find("BUSID") != std::string::npos &&
            trimmed.find("VID:PID") != std::string::npos) {
            inDataSection = true;
            continue;
        }

        if (!inDataSection) continue;

        // Parse data line
        auto info = ParseDeviceLine(trimmed);
        if (!info.busId.empty()) {
            devices.push_back(info);
        }
    }

    Logger::Instance().Debug("[UsbipdManager] Found " +
                             std::to_string(devices.size()) + " devices");

    // 根据设备状态更新状态机
    bool hasBound = false;
    bool hasAttached = false;
    for (const auto& d : devices) {
        if (d.IsAttached()) {
            hasAttached = true;
        }
        // state string like "Attached - Ubuntu" or persisted state
        if (d.state.find("Attached") != std::string::npos ||
            d.state.find("Persisted") != std::string::npos) {
            hasBound = true;
        }
    }

    if (hasAttached)
        state_ = UsbipdState::DeviceAttached;
    else if (hasBound)
        state_ = UsbipdState::DeviceBound;
    else if (!devices.empty())
        state_ = UsbipdState::DeviceRecognized;

    return devices;
}

bool UsbipdManager::Attach(const std::string& busId, const std::string& distro) {
    std::string cmd = "usbipd attach --wsl";
    if (!distro.empty()) {
        cmd += " " + distro;
    }
    cmd += " --busid " + busId;

    Logger::Instance().Info("[UsbipdManager] Attaching " + busId +
                            (distro.empty() ? "" : " to " + distro));

    auto result = executor_->Execute(cmd, "", 30000);
    if (result.exitCode != 0) {
        lastError_ = ClassifyError(result);
        Logger::Instance().Error("[UsbipdManager] Attach failed: " +
                                 result.stdOut + " | " + result.stdErr +
                                 " -> " + lastError_.errorType);
        state_ = UsbipdState::Error;
        return false;
    }

    state_ = UsbipdState::DeviceAttached;
    Logger::Instance().Info("[UsbipdManager] Attach succeeded for " + busId);
    return true;
}

bool UsbipdManager::Detach(const std::string& busId) {
    std::string cmd = "usbipd detach --busid " + busId;

    Logger::Instance().Info("[UsbipdManager] Detaching " + busId);

    auto result = executor_->Execute(cmd, "", 15000);
    if (result.exitCode != 0) {
        // If device already detached, treat as success (idempotent)
        if (result.stdOut.find("not attached") != std::string::npos ||
            result.stdErr.find("not attached") != std::string::npos) {
            Logger::Instance().Info("[UsbipdManager] " + busId + " was already detached");
            state_ = UsbipdState::DeviceRecognized;
            return true;
        }
        lastError_ = ClassifyError(result);
        Logger::Instance().Error("[UsbipdManager] Detach failed: " +
                                 result.stdOut + " | " + result.stdErr +
                                 " -> " + lastError_.errorType);
        state_ = UsbipdState::Error;
        return false;
    }

    state_ = UsbipdState::DeviceRecognized;
    Logger::Instance().Info("[UsbipdManager] Detach succeeded for " + busId);
    return true;
}

bool UsbipdManager::Bind(const std::string& busId) {
    std::string cmd = "usbipd bind --busid " + busId;
    auto result = executor_->Execute(cmd, "", 15000);
    if (result.exitCode != 0) {
        lastError_ = ClassifyError(result);
        Logger::Instance().Error("[UsbipdManager] Bind failed: " +
                                 result.stdOut + " | " + result.stdErr +
                                 " -> " + lastError_.errorType);
        state_ = UsbipdState::Error;
        return false;
    }
    state_ = UsbipdState::DeviceBound;
    return true;
}

bool UsbipdManager::Unbind(const std::string& busId) {
    std::string cmd = "usbipd unbind --busid " + busId;
    auto result = executor_->Execute(cmd, "", 15000);
    if (result.exitCode != 0) {
        lastError_ = ClassifyError(result);
        Logger::Instance().Error("[UsbipdManager] Unbind failed: " +
                                 result.stdOut + " | " + result.stdErr +
                                 " -> " + lastError_.errorType);
        state_ = UsbipdState::Error;
        return false;
    }
    state_ = UsbipdState::DeviceUnbound;
    return true;
}

UsbipdDeviceInfo UsbipdManager::ParseDeviceLine(const std::string& line) {
    UsbipdDeviceInfo info;

    if (line.empty()) return info;

    auto tokens = utils::Split(line, ' ');
    if (tokens.size() < 4) return info;

    info.busId = tokens[0];
    if (info.busId.empty()) return info;

    info.vidPid = tokens[1];

    size_t vidPidPos = line.find(info.vidPid);
    if (vidPidPos == std::string::npos) return info;
    size_t afterVidPid = vidPidPos + info.vidPid.size();

    size_t statePos = std::string::npos;
    std::vector<std::string> stateMarkers = { "Not attached", "Attached" };
    for (const auto& marker : stateMarkers) {
        statePos = line.find(marker, afterVidPid);
        if (statePos != std::string::npos) break;
    }

    if (statePos == std::string::npos) {
        info.description = utils::Trim(line.substr(afterVidPid));
        info.state = "Unknown";
        return info;
    }

    info.description = utils::Trim(line.substr(afterVidPid, statePos - afterVidPid));
    info.state = utils::Trim(line.substr(statePos));

    if (info.IsAttached()) {
        size_t dashPos = info.state.find('-');
        if (dashPos != std::string::npos) {
            info.wslDistro = utils::Trim(info.state.substr(dashPos + 1));
        }
    }

    return info;
}
