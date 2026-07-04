#pragma once
#include "../command/ICommandExecutor.h"
#include <string>
#include <vector>

struct WslDistribution {
    std::string name;       // "Ubuntu"
    bool isDefault;
    bool isRunning;
    std::string version;    // "1" or "2"

    bool IsWsl2() const { return version == "2"; }
};

class WslManager {
public:
    explicit WslManager(ICommandExecutor* executor);

    // Set the WSL distro for in-WSL commands (lsblk, mount, etc.)
    void SetDistro(const std::string& distro) { distro_ = distro; }

    // 枚举安装的 WSL 发行版
    std::vector<WslDistribution> ListDistributions();

    // 获取默认发行版
    WslDistribution GetDefaultDistribution();

    // 检查发行版是否为 WSL2（只有 WSL2 支持 usbipd 设备挂载）
    bool IsExt4Supported(const std::string& distro);

    // 在 WSL 内检测新接入的块设备（usbipd attach 后调用）
    // 返回设备名如 "sda"、"sdb"，空表示未找到
    std::string DetectBlockDevice(const std::string& previousDisks);

    // 确保 usb_storage 内核模块已加载（usbipd attach 前调用）
    bool EnsureUsbStorageModule();

    // 获取挂载前的磁盘列表（用于对比检测新设备）
    std::string GetDiskList();

    // 在 WSL 内挂载 EXT4 设备，返回错误描述（空=成功）
    // 可通过 isAuthError 判断是否为 sudo 密码错误
    std::string MountExt4(const std::string& device,     // "sda1"
                          const std::string& mountPoint,
                          const std::string& sudoPassword,
                          bool& isAuthError);

    // 在 WSL 内检查目录是否为挂载点
    bool IsMounted(const std::string& mountPoint);

    // 在 WSL 内卸载
    bool Unmount(const std::string& mountPoint,
                 const std::string& sudoPassword);

    // 构造 WSL 网络路径
    static std::string MakeWindowsPath(const std::string& distro,
                                       const std::string& mountPoint);

private:
    ICommandExecutor* executor_;
    std::string distro_;
};
