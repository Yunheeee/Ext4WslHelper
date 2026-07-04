#include "MountManager.h"
#include "../common/Logger.h"
#include "../common/Utils.h"
#include <algorithm>
#include <sstream>
#include <chrono>
#include <iomanip>

MountManager::MountManager(UsbipdManager* usbipd, WslManager* wsl)
    : usbipd_(usbipd), wsl_(wsl) {
}

MountSession MountManager::AttachAndMount(const std::string& busId,
                                           const std::string& distro,
                                           const std::string& sudoPassword) {
    MountSession session{};
    session.busId = busId;
    session.distro = distro;
    session.isMounted = false;
    session.status = MountSession::Status::Attaching;

    Logger::Instance().Info("[MountManager] Starting mount: " + busId + " -> " + distro);

    // Set the distro context for WSL commands
    wsl_->SetDistro(distro);

    // Step 0: 挂载前先清理 /mnt/sdcard 残留挂载（避免上次异常退出的脏状态）
    std::string mountPoint = GenerateMountPoint();
    if (wsl_->IsMounted(mountPoint)) {
        Logger::Instance().Info("[MountManager] " + mountPoint + " has stale mount, cleaning up");
        if (!wsl_->Unmount(mountPoint, sudoPassword)) {
            session.status = MountSession::Status::Error;
            session.errorDetail = "WSL umount 失败：" + mountPoint + " 无法卸载";
            Logger::Instance().Error("[MountManager] " + session.errorDetail);
            if (callback_) callback_(session);
            return session;
        }
    }

    // Step 1: Get disk list before attach (to detect new device after)
    std::string disksBefore = wsl_->GetDiskList();

    // Step 1.5: 加载 usb_storage 驱动（WSL VM 由后续命令自动启动）
    if (!wsl_->EnsureUsbStorageModule()) {
        session.status = MountSession::Status::Error;
        session.errorDetail = "usb_storage 内核模块未加载，usbipd 设备无法识别";
        Logger::Instance().Error("[MountManager] " + session.errorDetail);
        if (callback_) callback_(session);
        return session;
    }

    // Step 2: 绑定并 attach 设备到 WSL
    usbipd_->Bind(busId);
    if (!usbipd_->Attach(busId, distro)) {
        session.status = MountSession::Status::Error;
        Logger::Instance().Error("[MountManager] usbipd attach failed");
        if (callback_) callback_(session);
        return session;
    }

    // Step 3: Wait for block device to appear in WSL
    // The device may take a moment to be recognized by the WSL kernel
    // 重型 SD 读卡器识别可能较慢，给 20 次重试 ≈ 10s
    std::string device;
    for (int retry = 0; retry < 20; retry++) {
        Sleep(500);
        device = wsl_->DetectBlockDevice(disksBefore);
        if (!device.empty()) break;
    }

    if (device.empty()) {
        Logger::Instance().Error("[MountManager] No new block device detected after attach");
        // Rollback: detach
        usbipd_->Detach(busId);
        session.status = MountSession::Status::Error;
        if (callback_) callback_(session);
        return session;
    }

    session.device = device;

    // Step 4: Try mounting partition candidates
    std::string mountTarget = device;
    std::string candidate1 = device + "1";
    std::string candidateP1 = device + "p1";

    // We'll try mounting candidate1 first, then candidateP1, then the raw device

    // Create mount point and mount
    bool mounted = false;
    bool authError = false;
    std::string mountErr;

    // Try partition 1 (most common for SD cards)
    mountErr = wsl_->MountExt4(candidate1, mountPoint, sudoPassword, authError);
    if (mountErr.empty()) {
        mountTarget = candidate1;
        mounted = true;
    }
    // Try partition p1 (e.g., nvme0n1p1)
    if (!mounted) {
        mountErr = wsl_->MountExt4(candidateP1, mountPoint, sudoPassword, authError);
        if (mountErr.empty()) {
            mountTarget = candidateP1;
            mounted = true;
        }
    }

    if (!mounted) {
        Logger::Instance().Error("[MountManager] All mount attempts failed for " + device);
        usbipd_->Detach(busId);
        session.status = MountSession::Status::Error;
        session.isAuthError = authError;
        session.errorDetail = mountErr;
        if (callback_) callback_(session);
        return session;
    }

    session.device = mountTarget;
    session.mountPoint = mountPoint;
    session.windowsPath = WslManager::MakeWindowsPath(distro, mountPoint);
    session.isMounted = true;
    session.status = MountSession::Status::Mounted;

    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        sessions_.push_back(session);
    }

    Logger::Instance().Info("[MountManager] Mount complete: " + busId + " -> " + session.windowsPath);

    if (callback_) callback_(session);
    return session;
}
bool MountManager::UnmountAndDetach(const std::string& busId,
                                     const std::string& sudoPassword) {
    Logger::Instance().Info("[MountManager] Unmounting " + busId);

    // Find the session
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [&busId](const MountSession& s) { return s.busId == busId; });

        if (it == sessions_.end()) {
            // Session not tracked; attempt to detach anyway (idempotent)
            Logger::Instance().Warning("[MountManager] Session not found for " + busId +
                                       ", attempting detach only");
            return usbipd_->Detach(busId);
        }

        it->status = MountSession::Status::Disconnecting;

        // Step 1: Unmount in WSL
        if (it->isMounted) {
            wsl_->SetDistro(it->distro);
            wsl_->Unmount(it->mountPoint, sudoPassword);
        }

        // Step 2: usbipd detach
        bool detachOk = usbipd_->Detach(busId);

        // Remove from active sessions
        it->isMounted = false;
        it->status = MountSession::Status::Idle;

        sessions_.erase(it);

        Logger::Instance().Info("[MountManager] Unmount complete for " + busId);

        if (callback_) {
            MountSession closed{};
            closed.busId = busId;
            closed.status = MountSession::Status::Idle;
            callback_(closed);
        }

        return detachOk;
    }
}

std::vector<MountSession> MountManager::ListActiveSessions() {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    std::vector<MountSession> active;
    for (const auto& s : sessions_) {
        if (s.isMounted) {
            active.push_back(s);
        }
    }
    return active;
}

MountSession MountManager::GetSession(const std::string& busId) {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    for (const auto& s : sessions_) {
        if (s.busId == busId) return s;
    }
    return {};
}

void MountManager::SetNotificationCallback(
    std::function<void(const MountSession&)> callback) {
    callback_ = callback;
}

void MountManager::CleanupDisconnected(
    std::function<std::vector<std::string>()> enumeratePresentBusIds) {
    auto presentBusIds = enumeratePresentBusIds();

    std::lock_guard<std::mutex> lock(sessionsMutex_);
    std::vector<std::string> toRemove;
    for (const auto& session : sessions_) {
        if (!session.isMounted) continue;

        // Check if the device is still present via usbipd
        bool stillPresent = false;
        for (const auto& presentId : presentBusIds) {
            if (presentId == session.busId) {
                stillPresent = true;
                break;
            }
        }

        if (!stillPresent) {
            Logger::Instance().Warning("[MountManager] Device " + session.busId +
                                       " disconnected, scheduling cleanup");
            toRemove.push_back(session.busId);
        }
    }

    // Clean up (note: this is best-effort since we may not have sudo password cached)
    for (const auto& busId : toRemove) {
        auto it = std::find_if(sessions_.begin(), sessions_.end(),
            [&busId](const MountSession& s) { return s.busId == busId; });
        if (it != sessions_.end()) {
            // Mark as disconnected but keep in sessions for reference
            it->isMounted = false;
            it->status = MountSession::Status::Error;

            if (callback_) callback_(*it);
        }
    }
}

std::string MountManager::GenerateMountPoint() {
    return "/mnt/sdcard";
}
