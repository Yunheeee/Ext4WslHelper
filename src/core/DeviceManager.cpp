#include "DeviceManager.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include <sstream>

DeviceManager::DeviceManager(ICommandExecutor* executor)
    : executor_(executor) {
}

std::vector<UsbDeviceInfo> DeviceManager::EnumerateDevices() {
    return FetchFromUsbipd();
}

bool DeviceManager::Refresh() {
    auto currentDevices = FetchFromUsbipd();

    newDevices_.clear();
    removedDevices_.clear();

    // First call: just initialize the cache, no change signaling
    if (cachedDevices_.empty()) {
        cachedDevices_ = std::move(currentDevices);
        return false;
    }

    // Detect removed devices
    for (const auto& prev : cachedDevices_) {
        bool found = false;
        for (const auto& curr : currentDevices) {
            if (curr.busId == prev.busId) {
                found = true;
                break;
            }
        }
        if (!found) {
            removedDevices_.push_back(prev);
        }
    }

    // Detect new devices
    for (const auto& curr : currentDevices) {
        bool found = false;
        for (const auto& prev : cachedDevices_) {
            if (curr.busId == prev.busId) {
                found = true;
                break;
            }
        }
        if (!found) {
            newDevices_.push_back(curr);
        }
    }

    bool changed = !newDevices_.empty() || !removedDevices_.empty();
    cachedDevices_ = std::move(currentDevices);

    if (changed && changeCallback_) {
        changeCallback_();
    }

    return changed;
}

std::vector<std::string> DeviceManager::GetPresentBusIds() {
    std::vector<std::string> busIds;
    auto devices = FetchFromUsbipd();
    for (const auto& dev : devices) {
        busIds.push_back(dev.busId);
    }
    return busIds;
}

std::vector<UsbDeviceInfo> DeviceManager::FetchFromUsbipd() {
    std::vector<UsbDeviceInfo> devices;

    auto result = executor_->Execute("usbipd list", "", 15000);
    if (result.exitCode != 0) {
        Logger::Instance().Error("[DeviceManager] usbipd list failed: " + result.stdErr);
        return devices;
    }

    std::istringstream stream(result.stdOut);
    std::string line;
    bool inDataSection = false;

    while (std::getline(stream, line)) {
        std::string trimmed = utils::Trim(line);

        if (trimmed.empty() || trimmed == "Connected:" || trimmed == "Persisted:") {
            inDataSection = false;
            continue;
        }

        if (trimmed.find("BUSID") != std::string::npos &&
            trimmed.find("VID:PID") != std::string::npos) {
            inDataSection = true;
            continue;
        }

        if (!inDataSection) continue;

        // 使用 UsbipdManager 的共享解析逻辑（唯一解析入口）
        auto info = UsbipdManager::ParseDeviceLine(trimmed);
        if (!info.busId.empty()) {
            devices.push_back(info);
        }
    }

    return devices;
}
