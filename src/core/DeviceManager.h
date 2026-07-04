#pragma once
#include "../command/ICommandExecutor.h"
#include "UsbipdManager.h"
#include <vector>
#include <string>
#include <functional>

// UsbDeviceInfo 与 UsbipdDeviceInfo 同构，共享同一份解析逻辑
using UsbDeviceInfo = UsbipdDeviceInfo;

class DeviceManager {
public:
    explicit DeviceManager(ICommandExecutor* executor);

    // Enumerate all USB devices via usbipd list
    std::vector<UsbDeviceInfo> EnumerateDevices();

    // Refresh and detect hotplug changes. Returns true if list changed.
    bool Refresh();

    // Devices added since last Refresh()
    const std::vector<UsbDeviceInfo>& GetNewDevices() const { return newDevices_; }

    // Devices removed since last Refresh()
    const std::vector<UsbDeviceInfo>& GetRemovedDevices() const { return removedDevices_; }

    // Get bus IDs of currently present devices (for cleanup detection)
    std::vector<std::string> GetPresentBusIds();

    using ChangeCallback = std::function<void()>;
    void SetChangeCallback(ChangeCallback callback) { changeCallback_ = std::move(callback); }

private:
    ICommandExecutor* executor_;
    std::vector<UsbDeviceInfo> cachedDevices_;
    std::vector<UsbDeviceInfo> newDevices_;
    std::vector<UsbDeviceInfo> removedDevices_;
    ChangeCallback changeCallback_;

    std::vector<UsbDeviceInfo> FetchFromUsbipd();
};
