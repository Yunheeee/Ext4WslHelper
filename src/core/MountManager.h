#pragma once
#include "../command/ICommandExecutor.h"
#include "UsbipdManager.h"
#include "WslManager.h"
#include <string>
#include <vector>
#include <functional>
#include <mutex>

struct MountSession {
    std::string busId;
    std::string distro;
    std::string device;         // "sda1"
    std::string mountPoint;     // "/mnt/ext4-sd-card"
    std::string windowsPath;    // "\\\\wsl.localhost\\Ubuntu\\mnt\\ext4-sd-card"
    std::string errorDetail;    // error description if mount failed
    bool isMounted;
    bool isAuthError;           // true if failure was due to sudo password

    enum class Status {
        Idle,
        Attaching,
        Mounted,
        Disconnecting,
        Error
    };
    Status status;
};

class MountManager {
public:
    MountManager(UsbipdManager* usbipd, WslManager* wsl);

    // 完整流程: usbipd attach → 检测设备 → mount → 返回 session
    MountSession AttachAndMount(const std::string& busId,
                                const std::string& distro,
                                const std::string& sudoPassword);

    // 完整回滚: umount → usbipd detach
    bool UnmountAndDetach(const std::string& busId,
                          const std::string& sudoPassword);

    // 当前活跃会话列表
    std::vector<MountSession> ListActiveSessions();

    // 按 busId 获取会话
    MountSession GetSession(const std::string& busId);

    // 事件通知
    void SetNotificationCallback(std::function<void(const MountSession&)> callback);

    // 清理已断开会话（热插拔检测后调用）
    void CleanupDisconnected(std::function<std::vector<std::string>()> enumeratePresentBusIds);

private:
    std::string GenerateMountPoint();

    UsbipdManager* usbipd_;
    WslManager* wsl_;
    std::vector<MountSession> sessions_;
    std::mutex sessionsMutex_;
    std::function<void(const MountSession&)> callback_;
};
